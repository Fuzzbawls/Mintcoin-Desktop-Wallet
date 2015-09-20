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
void static CleanUpThread(void* parg);
void static SliceUpThread(void* parg);

static map<int, COldBlockIndex*> mapOldBlockIndex;
static queue<vector<pair<int, CBlock*> > > blockCache;
static queue<vector<pair<int, CBlock*> > > usedBlockCache;
static queue<vector<pair<int, COldBlockIndex*> > > indexCache;
static queue<vector<pair<int, COldBlockIndex*> > > usedIndexCache;
static bool mapEmpty = false;
static bool complete = false;
static int stored = 0;
static int tip = 0;
static int blocksChecked = 0;
static CCriticalSection cs_queue;
static CDBConverter *db;

COldBlockIndex *CDBConverter::CreateBlockIndex(int height)
{


    map<int, COldBlockIndex*>::iterator mi;
    /*if (mi != mapOldBlockIndex.end())
        return (*mi).second;*/

    // Create new
    COldBlockIndex* pindexNew = new COldBlockIndex();
    if (!pindexNew)
        throw runtime_error("LoadBlockIndex() : new CBlockIndex failed");
    mi = mapOldBlockIndex.insert(make_pair(height, pindexNew)).first;

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

        if(diskindex.nHeight < stored)
        {
            iterator->Next();
            continue;
        } 
        uint256 hash = diskindex.GetBlockHash();
        // Construct block index object
        COldBlockIndex* pindexNew = CreateBlockIndex(diskindex.nHeight);
        pindexNew->nFile          = diskindex.nFile;
        pindexNew->nBlockPos      = diskindex.nBlockPos;
        pindexNew->phashBlock     = &hash;
        
        tip++;
        iterator->Next();
    }
    delete iterator;
    
    return true;
}

bool CDBConverter::BlockConversion()
{
    stored = blocksChecked = mapBlockIndex.size();
    //Start here
    if(!LoadBlockIndex())
        return false;

    db = this;
       
    // Feeder
    NewThread(SliceUpThread, NULL);
    // Block check thread
    NewThread(CheckBlockThread, NULL);
    
    // disk write thread
    while(blockCache.size() < 5) {} // giving the Check thread a head start
    NewThread(AddBlockThread, NULL);
    NewThread(CleanUpThread,NULL);
    while(!fRequestShutdown)
    {
        // Progrss updates here
        sleep(100); // Sleep for now
        if(stored == tip)
            break;
    }
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
    bool mtWait = false;
    int working = 0;
    vector<pair<int, CBlock*> > vNewWork;
    vector<pair<int, COldBlockIndex*> > vOldIndex;
    
    while(1)
    {
        vOldIndex.reserve(500);
        vNewWork.reserve(100);

        {
            LOCK(cs_queue);
            if(indexCache.empty())
                mtWait =true;
            else
            {
                vOldIndex = indexCache.front();
                indexCache.pop();
            }
        }

        if(mtWait)
        {
            mtWait = false;
            sleep(1);
            continue;
        }

        BOOST_FOREACH(const PAIRTYPE(int, COldBlockIndex*)& item, vOldIndex)
        {
            COldBlockIndex* pindex = item.second;
    
            if(item.first < blocksChecked)
                continue;
            // Preliminary checks
            CBlock *newBlock = new CBlock();        
            if(!db->ReadFromDisk(pindex, newBlock))
                db->Fail(error("ProcessBlock() : ReadFromDisk FAILED"));
            if (!newBlock->CheckBlock())
                db->Fail(error("ProcessBlock() : CheckBlock FAILED"));
    
            vNewWork.push_back(make_pair(blocksChecked++, newBlock));
            working++;

            if(working == 100)
            {
                working = 0;
                LOCK(cs_queue);
                blockCache.push(vNewWork);
                vNewWork.clear();

            }
        }
        {
            LOCK(cs_queue);
            usedIndexCache.push(vOldIndex);
        }
        vector<pair<int, CBlock*> >().swap(vNewWork);
        vector<pair<int, COldBlockIndex*> >().swap(vOldIndex);
        if(blocksChecked == tip)
            break;
    }
    mapEmpty = true;
}
void static AddBlockThread(void* parg)
{
    // start
    while(1)
    {
        vector<pair<int, CBlock*> > vChunk;
        if(!blockCache.empty())
        {
            {
                LOCK(cs_queue);
                vChunk = blockCache.front();
                blockCache.pop();
            }
            BOOST_FOREACH(const PAIRTYPE(int, CBlock*)& item, vChunk)
            {
                                
                if (!db->ProcessBlock(item.second))
                    db->Fail(error("CBlock::ReadFromDisk() : OpenBlockFile failed"));
            }

            {
                LOCK(cs_queue);
                usedBlockCache.push(vChunk);
            }
            vector<pair<int, CBlock*> >().swap(vChunk);
        }
        if(stored == tip)
        {
            complete = true;
            break;
        }
    }
}

void static SliceUpThread(void* parg)
{
    // Start
    while(1)
    {
        vector<pair<int, COldBlockIndex*> > vIndex;
        vIndex.reserve(500);
        map<int, COldBlockIndex*>::iterator it = mapOldBlockIndex.begin();
        for(int i=0; i<500;i++, it++)
        {
            if(it == mapOldBlockIndex.end())
            {           
                mapEmpty = true;
                break;
            }
            vIndex.push_back(make_pair(it->first, it->second));
        }

        {
            LOCK(cs_queue);
            indexCache.push(vIndex);
        }
        vector<pair<int, COldBlockIndex*> >().swap(vIndex);
        mapOldBlockIndex.erase(mapOldBlockIndex.begin(), it);
        if(mapEmpty)
            break;
    }
}
void static CleanUpThread(void* parg)
{
    // Start
    while(1)
    {
        if(!usedIndexCache.empty())
        {
            vector<pair<int, COldBlockIndex*> > vtemp;
            {
                LOCK(cs_queue);
                vtemp = usedIndexCache.front();
                usedIndexCache.pop();
            }
            BOOST_FOREACH(PAIRTYPE(int, COldBlockIndex*) & it, vtemp)
            {
                COldBlockIndex *t = it.second;
                delete t;
            }vector<pair<int, COldBlockIndex*> >().swap(vtemp);
        }

        if(!usedBlockCache.empty())
        {
            vector<pair<int, CBlock*> > vtemp;
            {
                LOCK(cs_queue);
                vtemp = usedBlockCache.front();
                usedBlockCache.pop();
            }
            BOOST_FOREACH(PAIRTYPE(int, CBlock*) & it, vtemp)
            {
                CBlock *t = it.second;
                delete t;
            }vector<pair<int, CBlock*> >().swap(vtemp);
        }
        //sleep(10);
        if(complete == true)
            break;
    }
}
