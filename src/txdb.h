// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TXDB_H
#define BITCOIN_TXDB_H


#include "main.h"
#include "leveldb.h"

//#include <map>
#include <string>
//#include <vector>



/** CCoinsView backed by another CCoinsView */
/*class CCoinsViewBacked : public CCoinsView
{
protected:
    CCoinsView *base;

public:
    CCoinsViewBacked(CCoinsView &viewIn);
    bool GetCoins(uint256 txid, CCoins &coins);
    bool SetCoins(uint256 txid, const CCoins &coins);
    bool HaveCoins(uint256 txid);
    CBlockIndex *GetBestBlock();
    bool SetBestBlock(CBlockIndex *pindex);
    void SetBackend(CCoinsView &viewIn);
    bool BatchWrite(const std::map<uint256, CCoins> &mapCoins, CBlockIndex *pindex);
    bool GetStats(CCoinsStats &stats);
};*/

/** CCoinsView backed by the LevelDB coin database (coins/) */
class CCoinsViewDB : public CCoinsView
{
protected:
    CLevelDB db;
public:
    CCoinsViewDB(bool fMemory = false);

    bool GetCoins(uint256 txid, CCoins &coins);
    bool SetCoins(uint256 txid, const CCoins &coins);
    bool HaveCoins(uint256 txid);
    CBlockIndex *GetBestBlock();
    bool SetBestBlock(CBlockIndex *pindex);
    bool BatchWrite(const std::map<uint256, CCoins> &mapCoins, CBlockIndex *pindex);
    bool GetStats(CCoinsStats &stats);
};

/** Access to the block database (blktree/) */
class CBlockTreeDB : public CLevelDB
{
public:
    CBlockTreeDB(bool fMemory = false); 
private:
    CBlockTreeDB(const CBlockTreeDB&);
    void operator=(const CBlockTreeDB&);
public:
    bool WriteBlockIndex(const CDiskBlockIndex& blockindex);
    bool ReadBestInvalidTrust(CBigNum& bnBestInvalidWork);
    bool WriteBestInvalidTrust(const CBigNum& bnBestInvalidWork);
    bool ReadBlockFileInfo(int nFile, CBlockFileInfo &fileinfo);
    bool WriteBlockFileInfo(int nFile, const CBlockFileInfo &fileinfo);
    bool ReadLastBlockFile(int &nFile);
    bool WriteLastBlockFile(int nFile);
    bool ReadSyncCheckpoint(uint256& hashCheckpoint);
    bool WriteSyncCheckpoint(uint256 hashCheckpoint);
    bool ReadCheckpointPubKey(std::string& strPubKey);
    bool WriteCheckpointPubKey(const std::string& strPubKey);
    bool GetStats(CCoinsStats &stats);
    bool LoadBlockIndexGuts();
};

// Database conversion object. 
class COldBlockIndex;
struct Transfer;


class CDBConverter
{

private:
    leveldb::DB *pdb;  // Points to the global instance.

    // A batch stores up writes and deletes for atomic application. When this
    // field is non-NULL, writes/deletes go there instead of directly to disk.
    leveldb::WriteBatch *activeBatch;
    leveldb::Options options;

    
    COldBlockIndex *pindexGenesisBlock = NULL;
    bool fOk;

public:
    CDBConverter(boost::filesystem::path directory)
    {        
        fOk = true;

        printf("Starting database conversion\n");

        leveldb::Options options;
        options = GetOptions(true);
        options.create_if_missing = false;
        
        printf("Opening LevelDB in %s\n", directory.string().c_str());
        leveldb::Status status = leveldb::DB::Open(options, directory.string(), &pdb);
        if (!status.ok())
            throw std::runtime_error(strprintf("CDBConverter(): error opening database environment %s", status.ToString().c_str()));
        
        printf("Opened LevelDB successfully\n");

    }

    void Fail(bool error)
    { fOk = error;}

    COldBlockIndex *CreateBlockIndex(int height);
    bool LoadBlockIndex();
    bool BlockConversion();
    bool ProcessBlock(CBlock *block);
    bool ReadFromDisk(Transfer *pindex, CBlock *newBlock);
    FILE* OpenBlockFile(unsigned int nFile, unsigned int nBlockPos, const char* pszMode);
    
};

//////////////////////////////////////////////////////////////////////////////
// Inner class pruned from older codebase
//
struct Transfer
{
    unsigned int nFile;
    unsigned int nBlockPos;
};

class COldBlockIndex
{
public:
    const uint256* phashBlock;
    COldBlockIndex* pprev;
    COldBlockIndex* pnext;
    unsigned int nFile;
    unsigned int nBlockPos;
    uint256 nChainTrust; // ppcoin: trust score of block chain
    int nHeight;

    int64 nMint;
    int64 nMoneySupply;

    unsigned int nFlags;  // ppcoin: block index flags
    uint64 nStakeModifier; // hash modifier for proof-of-stake
    unsigned int nStakeModifierChecksum; // checksum of index; in-memeory only

    // proof-of-stake specific fields
    COutPoint prevoutStake;
    unsigned int nStakeTime;
    uint256 hashProofOfStake;

    // block header
    int nVersion;
    uint256 hashMerkleRoot;
    unsigned int nTime;
    unsigned int nBits;
    unsigned int nNonce;

    enum
    {
        BLOCK_PROOF_OF_STAKE = (1 << 0), // is proof-of-stake block
        BLOCK_STAKE_ENTROPY  = (1 << 1), // entropy bit for stake modifier
        BLOCK_STAKE_MODIFIER = (1 << 2), // regenerated stake modifier
    };

    COldBlockIndex()
    {
        phashBlock = NULL;
        pprev = NULL;
        pnext = NULL;
        nFile = 0;
        nBlockPos = 0;
        nHeight = 0;
        nChainTrust = 0;
        nMint = 0;
        nMoneySupply = 0;
        nFlags = 0;
        nStakeModifier = 0;
        nStakeModifierChecksum = 0;
        hashProofOfStake = 0;
        prevoutStake.SetNull();
        nStakeTime = 0;

        nVersion       = 0;
        hashMerkleRoot = 0;
        nTime          = 0;
        nBits          = 0;
        nNonce         = 0;
    }

   bool IsProofOfStake() const
    {
        return (nFlags & BLOCK_PROOF_OF_STAKE);
    }

    void SetProofOfStake()
    {
        nFlags |= BLOCK_PROOF_OF_STAKE;
    }
};

class COldDiskBlockIndex : public COldBlockIndex
{
private:
    uint256 blockHash;
    
public:
    uint256 hashPrev;
    uint256 hashNext;

    COldDiskBlockIndex()
    {
        hashPrev = 0;
        hashNext = 0;
        blockHash = 0;
    }

    IMPLEMENT_SERIALIZE
    (
        if (!(nType & SER_GETHASH))
            READWRITE(nVersion);

        READWRITE(hashNext);
        READWRITE(nFile);
        READWRITE(nBlockPos);
        READWRITE(nHeight);
        READWRITE(nMint);
        READWRITE(nMoneySupply);
        READWRITE(nFlags);
        READWRITE(nStakeModifier);
        if (IsProofOfStake())
        {
            READWRITE(prevoutStake);
            READWRITE(nStakeTime);
            READWRITE(hashProofOfStake);
        }
        else if (fRead)
        {
            const_cast<COldDiskBlockIndex*>(this)->prevoutStake.SetNull();
            const_cast<COldDiskBlockIndex*>(this)->nStakeTime = 0;
            const_cast<COldDiskBlockIndex*>(this)->hashProofOfStake = 0;
        }

        // block header
        READWRITE(this->nVersion);
        READWRITE(hashPrev);
        READWRITE(hashMerkleRoot);
        READWRITE(nTime);
        READWRITE(nBits);
        READWRITE(nNonce);
        READWRITE(blockHash);
    )
};

#endif  // BITCOIN_TXDB_H
