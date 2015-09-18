// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#include <map>
#include <queue>

#include <boost/version.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include "checkpoints.h"
#include "txdb.h"
#include "kernel.h"

using namespace std;
using namespace boost;


void static BatchWriteCoins(CLevelDBBatch &batch, const uint256 &hash, const CCoins &coins) {
    if (coins.IsPruned())
        batch.Erase(make_pair('c', hash));
    else
        batch.Write(make_pair('c', hash), coins);
}

void static BatchWriteHashBestChain(CLevelDBBatch &batch, const uint256 &hash) {
    batch.Write('B', hash);
}

CCoinsViewDB::CCoinsViewDB(bool fMemory) : db(GetDataDir() / "coins", fMemory) {}

bool CCoinsViewDB::GetCoins(uint256 txid, CCoins &coins) { 
    return db.Read(make_pair('c', txid), coins); 
}

bool CCoinsViewDB::SetCoins(uint256 txid, const CCoins &coins) {
    CLevelDBBatch batch;
    BatchWriteCoins(batch, txid, coins);
    return db.WriteBatch(batch);
}

bool CCoinsViewDB::HaveCoins(uint256 txid) {
    return db.Exists(make_pair('c', txid)); 
}

CBlockIndex *CCoinsViewDB::GetBestBlock() {
    uint256 hashBestChain;
    if (!db.Read('B', hashBestChain))
        return NULL;
    std::map<uint256, CBlockIndex*>::iterator it = mapBlockIndex.find(hashBestChain);
    if (it == mapBlockIndex.end())
        return NULL;
    return it->second;
}

bool CCoinsViewDB::SetBestBlock(CBlockIndex *pindex) {
    CLevelDBBatch batch;
    BatchWriteHashBestChain(batch, pindex->GetBlockHash()); 
    return db.WriteBatch(batch);
}

bool CCoinsViewDB::BatchWrite(const std::map<uint256, CCoins> &mapCoins, CBlockIndex *pindex) {
    printf("Committing %u changed transactions to coin database...\n", (unsigned int)mapCoins.size());

    CLevelDBBatch batch;
    for (std::map<uint256, CCoins>::const_iterator it = mapCoins.begin(); it != mapCoins.end(); it++)
        BatchWriteCoins(batch, it->first, it->second);
    BatchWriteHashBestChain(batch, pindex->GetBlockHash());

    return db.WriteBatch(batch);
}

CBlockTreeDB::CBlockTreeDB(bool fMemory) : CLevelDB(GetDataDir() / "blktree", fMemory) {}

bool CBlockTreeDB::WriteBlockIndex(const CDiskBlockIndex& blockindex)
{
    return Write(make_pair('b', blockindex.GetBlockHash()), blockindex);
}

bool CBlockTreeDB::ReadBestInvalidTrust(CBigNum& bnBestInvalidTrust)
{
    return Read('I', bnBestInvalidTrust);
}

bool CBlockTreeDB::WriteBestInvalidTrust(const CBigNum& bnBestInvalidTrust)
{
    return Write('I', bnBestInvalidTrust);
}

bool CBlockTreeDB::WriteBlockFileInfo(int nFile, const CBlockFileInfo &info) {
    return Write(make_pair('f', nFile), info);
}

bool CBlockTreeDB::ReadBlockFileInfo(int nFile, CBlockFileInfo &info) {
    return Read(make_pair('f', nFile), info);
}

bool CBlockTreeDB::WriteLastBlockFile(int nFile) {
    return Write('l', nFile);
}

bool CBlockTreeDB::ReadLastBlockFile(int &nFile) {
    return Read('l', nFile);
}


bool CBlockTreeDB::ReadSyncCheckpoint(uint256& hashCheckpoint)
{
    return Read('s', hashCheckpoint);
}

bool CBlockTreeDB::WriteSyncCheckpoint(uint256 hashCheckpoint)
{
    return Write('s', hashCheckpoint);
}

bool CBlockTreeDB::ReadCheckpointPubKey(string& strPubKey)
{
    return Read('p', strPubKey);
}

bool CBlockTreeDB::WriteCheckpointPubKey(const string& strPubKey)
{
    return Write('p', strPubKey);
}

bool CCoinsViewDB::GetStats(CCoinsStats &stats) {
    leveldb::Iterator *pcursor = db.NewIterator();
    pcursor->SeekToFirst();

    while (pcursor->Valid()) {
        try {
            leveldb::Slice slKey = pcursor->key();
            CDataStream ssKey(slKey.data(), slKey.data()+slKey.size(), SER_DISK, CLIENT_VERSION);
            char chType;
            ssKey >> chType;
            if (chType == 'c' && !fRequestShutdown) {
                leveldb::Slice slValue = pcursor->value();
                CDataStream ssValue(slValue.data(), slValue.data()+slValue.size(), SER_DISK, CLIENT_VERSION);
                CCoins coins;
                ssValue >> coins;
                uint256 txhash;
                ssKey >> txhash;

                stats.nTransactions++;
                BOOST_FOREACH(const CTxOut &out, coins.vout) {
                    if (!out.IsNull())
                        stats.nTransactionOutputs++;
                }
                stats.nSerializedSize += 32 + slValue.size();
            }
            pcursor->Next();
        } catch (std::exception &e) {
            return error("%s() : deserialize error", __PRETTY_FUNCTION__);
        }
    }
    delete pcursor;
    stats.nHeight = GetBestBlock()->nHeight;
    return true;
}

bool CBlockTreeDB::LoadBlockIndexGuts()
{ 
    
    // The block index is an in-memory structure that maps hashes to on-disk
    // locations where the contents of the block can be found. Here, we scan it
    // out of the DB and into mapBlockIndex.
    leveldb::Iterator *pcursor = NewIterator();
    
    // Seek to start key.
    CDataStream ssStartKey(SER_DISK, CLIENT_VERSION);
    ssStartKey << make_pair('b', uint256(0));
    pcursor->Seek(ssStartKey.str());
    
    // Now read each entry.
    while (pcursor->Valid())
    {
        // Unpack keys and values.
        leveldb::Slice slKey = pcursor->key();
        CDataStream ssKey(slKey.data(), slKey.data()+slKey.size(), SER_DISK, CLIENT_VERSION);
        
        char chType;
        ssKey >> chType;

        if (fRequestShutdown || chType != 'b')
            break;
        
        leveldb::Slice slValue = pcursor->value();
        CDataStream ssValue(slValue.data(), slValue.data()+slValue.size(), SER_DISK, CLIENT_VERSION);
        
        CDiskBlockIndex diskindex;
        ssValue >> diskindex;

        // Construct block index object
        CBlockIndex* pindexNew    = InsertBlockIndex(diskindex.GetBlockHash());
        pindexNew->pprev          = InsertBlockIndex(diskindex.hashPrev);
        pindexNew->nFile          = diskindex.nFile;
        pindexNew->nHeight        = diskindex.nHeight;
        pindexNew->nDataPos       = diskindex.nDataPos;
        pindexNew->nUndoPos       = diskindex.nUndoPos;
        pindexNew->nMint          = diskindex.nMint;
        pindexNew->nMoneySupply   = diskindex.nMoneySupply;
        pindexNew->nFlags         = diskindex.nFlags;
        pindexNew->nStakeModifier = diskindex.nStakeModifier;
        pindexNew->prevoutStake   = diskindex.prevoutStake;
        pindexNew->nStakeTime     = diskindex.nStakeTime;
        pindexNew->hashProofOfStake = diskindex.hashProofOfStake;
        pindexNew->nVersion       = diskindex.nVersion;
        pindexNew->hashMerkleRoot = diskindex.hashMerkleRoot;
        pindexNew->nTime          = diskindex.nTime;
        pindexNew->nBits          = diskindex.nBits;
        pindexNew->nNonce         = diskindex.nNonce;
        pindexNew->nStatus        = diskindex.nStatus;
        pindexNew->nTx            = diskindex.nTx;

        // Watch for genesis block
        if (pindexGenesisBlock == NULL && diskindex.GetBlockHash() == hashGenesisBlock)
            pindexGenesisBlock = pindexNew;

        /*if (!pindexNew->CheckIndex()) {
            delete pcursor;
            return error("LoadBlockIndex() : CheckIndex failed: %s", pindexNew->ToString().c_str());
        }*/

        // NovaCoin: build setStakeSeen
        if (pindexNew->IsProofOfStake())
            setStakeSeen.insert(make_pair(pindexNew->prevoutStake, pindexNew->nStakeTime));

        pcursor->Next();
    }

    delete pcursor;
    return true;
}

//////////////////////////////////////////////////////////////////////////////
//
// CDBConverter
//


void static CheckBlockThread(void* parg);
void static AddBlockThread(void* parg);

static vector<pair<int, COldBlockIndex*> > vSortedByHeight;
static queue<vector<pair<int, CBlock*> > > blockCache;
static int working = 0;
static int stored = 0;
static int tip = 0;
static int blocksChecked = 0;
static CCriticalSection cs_queue;
static CDBConverter *db;

COldBlockIndex *CDBConverter::InsertBlockIndex(uint256 hash)
{
    if (hash == 0)
        return NULL;

    // Return existing
    map<uint256, COldBlockIndex*>::iterator mi = mapOldBlockIndex.find(hash);
    if (mi != mapOldBlockIndex.end())
        return (*mi).second;

    // Create new
    COldBlockIndex* pindexNew = new COldBlockIndex();
    if (!pindexNew)
        throw runtime_error("LoadBlockIndex() : new CBlockIndex failed");
    mi = mapOldBlockIndex.insert(make_pair(hash, pindexNew)).first;
    pindexNew->phashBlock = &((*mi).first);

    return pindexNew;
}

bool CDBConverter::LoadBlockIndex()
{
    // The block index is an in-memory structure that maps hashes to on-disk
    // locations where the contents of the block can be found. Here, we scan it
    // out of the DB and into mapOldBlockIndex.
    leveldb::Iterator *iterator = pdb->NewIterator(leveldb::ReadOptions());
    // Seek to start key.
    CDataStream ssStartKey(SER_DISK, CLIENT_VERSION);
    ssStartKey << make_pair(string("blockindex"), uint256(0));
    iterator->Seek(ssStartKey.str());
    // Now read each entry.
    while (iterator->Valid())
    {
        // Unpack keys and values.
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.write(iterator->key().data(), iterator->key().size());
        CDataStream ssValue(SER_DISK, CLIENT_VERSION);
        ssValue.write(iterator->value().data(), iterator->value().size());
        string strType;
        ssKey >> strType;
        // Did we reach the end of the data to read?
        if (fRequestShutdown || strType != "blockindex")
            break;
        COldDiskBlockIndex diskindex;
        ssValue >> diskindex;

        // Construct block index object
        COldBlockIndex* pindexNew = InsertBlockIndex(diskindex.GetBlockHash());
        pindexNew->pprev          = InsertBlockIndex(diskindex.hashPrev);
        pindexNew->pnext          = InsertBlockIndex(diskindex.hashNext);
        pindexNew->nFile          = diskindex.nFile;
        pindexNew->nBlockPos      = diskindex.nBlockPos;
        pindexNew->nHeight        = diskindex.nHeight;
        pindexNew->nMint          = diskindex.nMint;
        pindexNew->nMoneySupply   = diskindex.nMoneySupply;
        pindexNew->nFlags         = diskindex.nFlags;
        pindexNew->nStakeModifier = diskindex.nStakeModifier;
        pindexNew->prevoutStake   = diskindex.prevoutStake;
        pindexNew->nStakeTime     = diskindex.nStakeTime;
        pindexNew->hashProofOfStake = diskindex.hashProofOfStake;
        pindexNew->nVersion       = diskindex.nVersion;
        pindexNew->hashMerkleRoot = diskindex.hashMerkleRoot;
        pindexNew->nTime          = diskindex.nTime;
        pindexNew->nBits          = diskindex.nBits;
        pindexNew->nNonce         = diskindex.nNonce;

        // Watch for genesis block
        if (pindexGenesisBlock == NULL && diskindex.GetBlockHash() == hashGenesisBlock)
            pindexGenesisBlock = pindexNew;

        

        iterator->Next();
    }
    delete iterator;
    
    return true;
}

bool CDBConverter::BlockConversion()
{
    //Start here
    if(!LoadBlockIndex())
        return false;

    db = this;
    stored = blocksChecked = mapBlockIndex.size();

    
    vSortedByHeight.reserve(mapOldBlockIndex.size());
    BOOST_FOREACH(const PAIRTYPE(uint256, COldBlockIndex*)& item, mapOldBlockIndex)
    {
        COldBlockIndex* pindex = item.second;
        vSortedByHeight.push_back(make_pair(pindex->nHeight, pindex));
    }
    sort(vSortedByHeight.begin(), vSortedByHeight.end());
    tip = vSortedByHeight.size();

    // Block Checking thread
    NewThread(CheckBlockThread, this);
    


    // disk write thread
    while(blockCache.size() < 2) {} // Giving the Check thread a head start
    NewThread(AddBlockThread, NULL);
    while(blocksChecked < vSortedByHeight.size())
        sleep(5000); // Progrss updates here
    return true;
    
}

bool CDBConverter::ProcessBlock(CBlock *pblock)
{
    // Check for duplicate
    uint256 hash = pblock->GetHash();
    if (mapBlockIndex.count(hash))
        return true;
        //return error("ProcessBlock() : already have block %d %s", mapBlockIndex[hash]->nHeight, hash.ToString().substr(0,20).c_str());
    // ppcoin: check proof-of-stake
    // Limited duplicity on stake: prevents block flood attack
    // Duplicate stake allowed only when there is orphan child block
    //if (pblock->IsProofOfStake() && setStakeSeen.count(pblock->GetProofOfStake()) && !mapOrphanBlocksByPrev.count(hash) && !Checkpoints::WantedByPendingSyncCheckpoint(hash))
      //  return error("ProcessBlock() : duplicate proof-of-stake (%s, %d) for block %s", pblock->GetProofOfStake().first.ToString().c_str(), pblock->GetProofOfStake().second, hash.ToString().c_str());
    
    // ppcoin: verify hash target and signature of coinstake tx
    if (pblock->IsProofOfStake())
    {
        uint256 hashProofOfStake = 0;
        if (!CheckProofOfStake(pblock->vtx[1], pblock->nBits, hashProofOfStake))
        {
            printf("WARNING: ProcessBlock(): check proof-of-stake failed for block %s\n", hash.ToString().c_str());
            return false; // do not error here as we expect this during initial block download
        }
        if (!mapProofOfStake.count(hash)) // add to mapProofOfStake
            mapProofOfStake.insert(make_pair(hash, hashProofOfStake));
    }

    CBlockIndex* pcheckpoint = Checkpoints::GetLastSyncCheckpoint();
    if (pcheckpoint && pblock->hashPrevBlock != hashBestChain && !Checkpoints::WantedByPendingSyncCheckpoint(hash))
    {
        // Extra checks to prevent "fill up memory by spamming with bogus blocks"
        int64 deltaTime = pblock->GetBlockTime() - pcheckpoint->nTime;
        CBigNum bnNewBlock;
        bnNewBlock.SetCompact(pblock->nBits);
        CBigNum bnRequired;

        if (pblock->IsProofOfStake())
            bnRequired.SetCompact(ComputeMinStake(GetLastBlockIndex(pcheckpoint, true)->nBits, deltaTime, pblock->nTime));
        else
            bnRequired.SetCompact(ComputeMinWork(GetLastBlockIndex(pcheckpoint, false)->nBits, deltaTime));        
    }

    // Store to disk
    if (!pblock->AcceptBlock())
        return error("ProcessBlock() : AcceptBlock FAILED");
    stored++;
    printf("ProcessBlock: ACCEPTED\n");

    return true;
}

bool CDBConverter::ReadFromDisk(COldBlockIndex *pindex, CBlock *newBlock)
{
   // Open history file to read
    CAutoFile filein = CAutoFile(OpenBlockFile(pindex->nFile, pindex->nBlockPos, "rb"), SER_DISK, CLIENT_VERSION);
    if (!filein)
            return error("CBlock::ReadFromDisk() : OpenBlockFile failed");

    // Read block
    try {
        filein >> *newBlock;
    }
    catch (std::exception &e) {
        return error("%s() : deserialize or I/O error", __PRETTY_FUNCTION__);
    }

    if (newBlock->GetHash() != pindex->GetBlockHash())
        return error("CBlock::ReadFromDisk() : GetHash() doesn't match index"); 

    return true;
}

FILE* CDBConverter::OpenBlockFile(unsigned int nFile, unsigned int nBlockPos, const char* pszMode)
{
    string strBlockFn = strprintf("blk%04u.dat", nFile);
    filesystem::path blockFilePath = GetDataDir() / strBlockFn;

    if ((nFile < 1) || (nFile == (unsigned int) -1))
        return NULL;
    FILE* file = fopen(blockFilePath.string().c_str(), pszMode);
    if (!file)
        return NULL;
    if (nBlockPos != 0 && !strchr(pszMode, 'a') && !strchr(pszMode, 'w'))
    {
        if (fseek(file, nBlockPos, SEEK_SET) != 0)
        {
            fclose(file);
            return NULL;
        }
    }
    return file;
}

void static CheckBlockThread(void* parg)
{
    // start
    //std::shared_ptr<std::vector<T> > 
    vector<pair<int, CBlock*> > newWork;
    BOOST_FOREACH(const PAIRTYPE(int, COldBlockIndex*)& item, vSortedByHeight)
    {
        COldBlockIndex* pindex = item.second;

        if(pindex->nHeight < blocksChecked)
            continue;
        // Preliminary checks
        CBlock *newBlock = new CBlock();        
        db->ReadFromDisk(pindex, newBlock);
        if (!newBlock->CheckBlock())
            db->Fail(error("ProcessBlock() : CheckBlock FAILED"));

        newWork.push_back(make_pair(blocksChecked, newBlock)); 
        working++;
        blocksChecked++;
        if(working == 100)
        {
            LOCK(cs_queue);
            blockCache.push(newWork);
            working = 0;
            //newWork.clear();
            vector<pair<int, CBlock*> >().swap(newWork);
        }
        
    }
}
void static AddBlockThread(void* parg)
{
    // start
    while(stored < tip)
    {
        vector<pair<int, CBlock*> > vchunk;
        if(!blockCache.empty())
        {
            {
                LOCK(cs_queue);
                vchunk = blockCache.front();
                blockCache.pop();
            }
            BOOST_FOREACH(const PAIRTYPE(int, CBlock*)& item, vchunk)
            {
                if(item.second->GetHash()== hashGenesisBlock)
                    continue;
                
                if (!db->ProcessBlock(item.second))
                    db->Fail(error("CBlock::ReadFromDisk() : OpenBlockFile failed"));
            }
            vchunk.clear();
        }
    }
}

