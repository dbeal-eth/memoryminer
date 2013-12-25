#include <boost/unordered_map.hpp>
#include <iostream>
#include <openssl/sha.h>
#include "momentum.h"
#include <openssl/aes.h>
#include "main.h"
#include <openssl/evp.h>

namespace mc 
{
	#define PSUEDORANDOM_DATA_SIZE 30 //2^30 = 1GB
	#define PSUEDORANDOM_DATA_CHUNK_SIZE 6 //2^6 = 64 bytes
	#define L2CACHE_TARGET 16 // 2^16 = 64K
	#define AES_ITERATIONS 50

	// useful constants
	uint32_t psuedoRandomDataSize=(1<<PSUEDORANDOM_DATA_SIZE);
	uint32_t cacheMemorySize = (1<<L2CACHE_TARGET);
	uint32_t chunks=(1<<(PSUEDORANDOM_DATA_SIZE-PSUEDORANDOM_DATA_CHUNK_SIZE));
	uint32_t chunkSize=(1<<(PSUEDORANDOM_DATA_CHUNK_SIZE));
	uint32_t comparisonSize=(1<<(PSUEDORANDOM_DATA_SIZE-L2CACHE_TARGET));
	
	void static SHA512Filler(char *mainMemoryPsuedoRandomData, int threadNumber, int totalThreads,uint256 midHash, char* isComplete){
		
		
		//Generate psuedo random data to store in main memory
		unsigned char hash_tmp[sizeof(midHash)];
		memcpy((char*)&hash_tmp[0], (char*)&midHash, sizeof(midHash) );
		uint32_t* index = (uint32_t*)hash_tmp;
		
		uint32_t chunksToProcess=chunks/totalThreads;
		uint32_t startChunk=threadNumber*chunksToProcess;
		
		for( uint32_t i = startChunk; i < startChunk+chunksToProcess;  i++){
			*index = i;
			SHA512((unsigned char*)hash_tmp, sizeof(hash_tmp), (unsigned char*)&(mainMemoryPsuedoRandomData[i*chunkSize]));
			//This can take a while, so check periodically to see if we need to kill the thread
			/*if(i%100000==0){
				try{
					//If parent has requested termination
					boost::this_thread::interruption_point();
				}catch( boost::thread_interrupted const& e ){
         				throw e;
				}
			}*/
		}
		isComplete[threadNumber]=1;
	}
	
	void static aesSearch(char *mainMemoryPsuedoRandomData, int threadNumber, int totalThreads, char* isComplete, std::vector< std::pair<uint32_t,uint32_t> > *results){
		//Allocate temporary memory
		unsigned char *cacheMemoryOperatingData;
		unsigned char *cacheMemoryOperatingData2;	
		cacheMemoryOperatingData=new unsigned char[cacheMemorySize+16];
		cacheMemoryOperatingData2=new unsigned char[cacheMemorySize];
	
		//Create references to data as 32 bit arrays
		uint32_t* cacheMemoryOperatingData32 = (uint32_t*)cacheMemoryOperatingData;
		uint32_t* cacheMemoryOperatingData322 = (uint32_t*)cacheMemoryOperatingData2;
		uint32_t* mainMemoryPsuedoRandomData32 = (uint32_t*)mainMemoryPsuedoRandomData;
		
		//Search for pattern in psuedorandom data
		//
		
		unsigned char key[32] = {0};
		unsigned char iv[AES_BLOCK_SIZE];
		int outlen1, outlen2;
		unsigned int useEVP = GetArg("-useevp", 1);
		
		//Iterate over the data
		int searchNumber=comparisonSize/totalThreads;
		int startLoc=threadNumber*searchNumber;
		for(uint32_t k=startLoc;k<startLoc+searchNumber;k++){
			
			//This can take a while, so check periodically to see if we need to kill the thread
			/*if(k%100==0){
				try{
					//If parent has requested termination
					boost::this_thread::interruption_point();
				}catch( boost::thread_interrupted const& e ){
					//free memory
					delete [] cacheMemoryOperatingData;
					delete [] cacheMemoryOperatingData2;
					isComplete[threadNumber]=1;
					throw e;
				}
			}*/
			
			//copy 64k of data to first l2 cache
			memcpy((char*)&cacheMemoryOperatingData[0], (char*)&mainMemoryPsuedoRandomData[k*cacheMemorySize], cacheMemorySize);
			
			for(int j=0;j<AES_ITERATIONS;j++){

				//use last 4 bits of first cache as next location
				uint32_t nextLocation = cacheMemoryOperatingData32[(cacheMemorySize/4)-1]%comparisonSize;

				//Copy data from indicated location to second l2 cache -
				memcpy((char*)&cacheMemoryOperatingData2[0], (char*)&mainMemoryPsuedoRandomData[nextLocation*cacheMemorySize], cacheMemorySize);

				//XOR location data into second cache
				for(uint32_t i = 0; i < cacheMemorySize/4; i++){
					cacheMemoryOperatingData322[i] = cacheMemoryOperatingData32[i] ^ cacheMemoryOperatingData322[i];
				}

				//AES Encrypt using last 256bits of Xorred value as key
				//AES_set_encrypt_key((unsigned char*)&cacheMemoryOperatingData2[cacheMemorySize-32], 256, &AESkey);
				
				//Use last X bits as initial vector
				
				//AES CBC encrypt data in cache 2, place it into cache 1, ready for the next round
				//AES_cbc_encrypt((unsigned char*)&cacheMemoryOperatingData2[0], (unsigned char*)&cacheMemoryOperatingData[0], cacheMemorySize, &AESkey, iv, AES_ENCRYPT);
				if(useEVP){
					EVP_CIPHER_CTX ctx;
					memcpy(key,(unsigned char*)&cacheMemoryOperatingData2[cacheMemorySize-32],32);
					memcpy(iv,(unsigned char*)&cacheMemoryOperatingData2[cacheMemorySize-AES_BLOCK_SIZE],AES_BLOCK_SIZE);
					EVP_EncryptInit(&ctx, EVP_aes_256_cbc(), key, iv);
					EVP_EncryptUpdate(&ctx, cacheMemoryOperatingData, &outlen1, cacheMemoryOperatingData2, cacheMemorySize);
					EVP_EncryptFinal(&ctx, cacheMemoryOperatingData + outlen1, &outlen2);
					EVP_CIPHER_CTX_cleanup(&ctx);
				}else{
					AES_KEY AESkey;
					AES_set_encrypt_key((unsigned char*)&cacheMemoryOperatingData2[cacheMemorySize-32], 256, &AESkey);			
					memcpy(iv,(unsigned char*)&cacheMemoryOperatingData2[cacheMemorySize-AES_BLOCK_SIZE],AES_BLOCK_SIZE);
					AES_cbc_encrypt((unsigned char*)&cacheMemoryOperatingData2[0], (unsigned char*)&cacheMemoryOperatingData[0], cacheMemorySize, &AESkey, iv, AES_ENCRYPT);
				}
				//printf("length: %d\n", sizeof(cacheMemoryOperatingData2));
			}
			
			//use last X bits as solution
			uint32_t solution=cacheMemoryOperatingData32[(cacheMemorySize/4)-1]%comparisonSize;
			//printf("solution - %d / %u \n",k,solution);
					
			if(solution==1968){
				uint32_t proofOfCalculation=cacheMemoryOperatingData32[(cacheMemorySize/4)-2];
				printf("hash generated - %d / %u / %u\n",k,solution,proofOfCalculation); // not needed really
				(*results).push_back( std::make_pair( k, proofOfCalculation ) );
			}
		}
		
		//free memory
		delete [] cacheMemoryOperatingData;
		delete [] cacheMemoryOperatingData2;
		//CRYPTO_cleanup_all_ex_data();
		//EVP_cleanup();
		isComplete[threadNumber]=1;
	}
	
	void waitForAllThreadsToComplete(char* threadsComplete,int totalThreads){
		int complete=0;
		float firstThreadFinishedTime=0;
		bool watchDogTimerFinish=false;
		do{
			MilliSleep(100);
			complete=0;
			for(int i=0;i<totalThreads;i++){
				if (threadsComplete[i]==1){
					complete++;
				}
			}
			if(complete>0 && firstThreadFinishedTime==0){
				firstThreadFinishedTime=(float)clock();
			}
			if(firstThreadFinishedTime>0 && (float)clock()-firstThreadFinishedTime>60000){
				//It's been over 60 seconds since the first thread completed - let's exit. This should happen very rarely.
				watchDogTimerFinish=true;
			}
		}while(complete!=totalThreads && !watchDogTimerFinish);
	}
	
 	std::vector< std::pair<uint32_t,uint32_t> > momentum_search( uint256 midHash, char *mainMemoryPsuedoRandomData, int totalThreads){
		
		//printf("Start Search\n"); // Not needed really
		//Take note of the current block, so we can interrupt the thread if a new block is found.
		CBlockIndex* pindexPrev = pindexBest;
		
		std::vector< std::pair<uint32_t,uint32_t> > results;
				
		//results=new vector< std::pair<uint32_t,uint32_t> >;
		//results=NULL;
		
		//clock_t t1 = clock();
		boost::thread_group* sha512Threads = new boost::thread_group();
		char *threadsComplete;
		threadsComplete=new char[totalThreads];
		for (int i = 0; i < totalThreads; i++){
			sha512Threads->create_thread(boost::bind(&SHA512Filler, mainMemoryPsuedoRandomData, i,totalThreads,midHash,threadsComplete));
		}
		//Wait for all threads to complete
		waitForAllThreadsToComplete(threadsComplete, totalThreads);
		
		//clock_t t2 = clock();
		//printf("create psuedorandom data %f\n",(float)t2-(float)t1);

		boost::thread_group* aesThreads = new boost::thread_group();
		threadsComplete=new char[totalThreads];
		for (int i = 0; i < totalThreads; i++){
			aesThreads->create_thread(boost::bind(&aesSearch, mainMemoryPsuedoRandomData, i,totalThreads,threadsComplete,&results));
		}
		//Wait for all threads to complete
		waitForAllThreadsToComplete(threadsComplete, totalThreads);
		
		
		//clock_t t3 = clock();
		//printf("comparisons %f\n",(float)t3-(float)t2);
		delete aesThreads;
		delete sha512Threads;
		return results;
	}
	
	
	
	bool momentum_verify( uint256 midHash, uint32_t a, uint32_t b ){
		//return false;
		
		clock_t t1 = clock();
		
		//Basic check
		if( a > comparisonSize ) return false;
		
		//Allocate memory required
		unsigned char *cacheMemoryOperatingData;
		unsigned char *cacheMemoryOperatingData2;	
		cacheMemoryOperatingData=new unsigned char[cacheMemorySize+16];
		cacheMemoryOperatingData2=new unsigned char[cacheMemorySize];
		uint32_t* cacheMemoryOperatingData32 = (uint32_t*)cacheMemoryOperatingData;
		uint32_t* cacheMemoryOperatingData322 = (uint32_t*)cacheMemoryOperatingData2;
		
		unsigned char  hash_tmp[sizeof(midHash)];
		memcpy((char*)&hash_tmp[0], (char*)&midHash, sizeof(midHash) );
		uint32_t* index = (uint32_t*)hash_tmp;
		
		//AES_KEY AESkey;
		//unsigned char iv[AES_BLOCK_SIZE];
		
		uint32_t startLocation=a*cacheMemorySize/chunkSize;
		uint32_t finishLocation=startLocation+(cacheMemorySize/chunkSize);
			
		//copy 64k of data to first l2 cache		
		for( uint32_t i = startLocation; i <  finishLocation;  i++){
			*index = i;
			SHA512((unsigned char*)hash_tmp, sizeof(hash_tmp), (unsigned char*)&(cacheMemoryOperatingData[(i-startLocation)*chunkSize]));
		}
		
		unsigned int useEVP = GetArg("-useevp", 1);
		unsigned char key[32] = {0};
		unsigned char iv[AES_BLOCK_SIZE];
		int outlen1, outlen2;
		
		//memset(cacheMemoryOperatingData2,0,cacheMemorySize);
		for(int j=0;j<AES_ITERATIONS;j++){
			
			//use last 4 bits as next location
			startLocation = (cacheMemoryOperatingData32[(cacheMemorySize/4)-1]%comparisonSize)*cacheMemorySize/chunkSize;
			finishLocation=startLocation+(cacheMemorySize/chunkSize);
			for( uint32_t i = startLocation; i <  finishLocation;  i++){
				*index = i;
				SHA512((unsigned char*)hash_tmp, sizeof(hash_tmp), (unsigned char*)&(cacheMemoryOperatingData2[(i-startLocation)*chunkSize]));
			}

			//XOR location data into second cache
			for(uint32_t i = 0; i < cacheMemorySize/4; i++){
				cacheMemoryOperatingData322[i] = cacheMemoryOperatingData32[i] ^ cacheMemoryOperatingData322[i];
			}
				
			//AES Encrypt using last 256bits as key
			
			if(useEVP){
				EVP_CIPHER_CTX ctx;
				memcpy(key,(unsigned char*)&cacheMemoryOperatingData2[cacheMemorySize-32],32);
				memcpy(iv,(unsigned char*)&cacheMemoryOperatingData2[cacheMemorySize-AES_BLOCK_SIZE],AES_BLOCK_SIZE);
				EVP_EncryptInit(&ctx, EVP_aes_256_cbc(), key, iv);
				EVP_EncryptUpdate(&ctx, cacheMemoryOperatingData, &outlen1, cacheMemoryOperatingData2, cacheMemorySize);
				EVP_EncryptFinal(&ctx, cacheMemoryOperatingData + outlen1, &outlen2);
				EVP_CIPHER_CTX_cleanup(&ctx);
			}else{
				AES_KEY AESkey;
				AES_set_encrypt_key((unsigned char*)&cacheMemoryOperatingData2[cacheMemorySize-32], 256, &AESkey);			
				memcpy(iv,(unsigned char*)&cacheMemoryOperatingData2[cacheMemorySize-AES_BLOCK_SIZE],AES_BLOCK_SIZE);
				AES_cbc_encrypt((unsigned char*)&cacheMemoryOperatingData2[0], (unsigned char*)&cacheMemoryOperatingData[0], cacheMemorySize, &AESkey, iv, AES_ENCRYPT);
			}
			
		}
			
		//use last X bits as solution
		uint32_t solution=cacheMemoryOperatingData32[(cacheMemorySize/4)-1]%comparisonSize;
		uint32_t proofOfCalculation=cacheMemoryOperatingData32[(cacheMemorySize/4)-2];
		//printf("verify solution - %d / %u / %u\n",a,solution,proofOfCalculation);
		
		//free memory
		delete [] cacheMemoryOperatingData;
		delete [] cacheMemoryOperatingData2;		
		//CRYPTO_cleanup_all_ex_data();
		//EVP_cleanup();
		
		if(solution==1968 && proofOfCalculation==b){
			return true;
		}
		
		return false;

	}

}
