//===
// by xolokram/TB
// 2013
//===

#include <iostream>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <map>

#include <sstream>

#include "serialize.h"
#include "bitcoinrpc.h"
#include "json/json_spirit_value.h"
#include "json/json_spirit_reader.h"
#include <boost/thread.hpp>
#include <boost/asio.hpp>
#include <boost/uuid/sha1.hpp>

#define VERSION_MAJOR 0
#define VERSION_MINOR 8
#define VERSION_EXT "RC1"

#define MAX_THREADS 128

// <START> be compatible to original code (not actually used!)
//#include "txdb.h"
#include "walletdb.h"
#include "wallet.h"
#include "ui_interface.h"
CWallet *pwalletMain;
CClientUIInterface uiInterface;
void StartShutdown() {
  exit(0);
}
// </END>

/*********************************
* global variables, structs and extern functions
*********************************/

extern CBlockIndex *pindexBest;
extern void BitcoinMiner(CWallet        *pwallet,
                         CBlockProvider *CBlockProvider,
                         unsigned int thread_id);
extern bool fPrintToConsole;
extern bool fDebug;

struct blockHeader_t {
  // comments: BYTES <index> + <length>
  int           nVersion;            // 0+4
  uint256       hashPrevBlock;       // 4+32
  uint256       hashMerkleRoot;      // 36+32
  unsigned int  nTime;               // 68+4
  unsigned int  nBits;               // 72+4
  unsigned int  nNonce;              // 76+4
  uint32_t nBirthdayA;               // 80+4
  uint32_t nBirthdayB;               // 84+4
};                                   // =88 bytes header (80 default + 8 birthday hashes)

size_t thread_num_max;
static size_t miner_id;
static boost::asio::ip::tcp::socket* socket_to_server;
static boost::posix_time::ptime t_start;
static std::map<int,unsigned long> statistics;
static bool running;
static volatile int submitting_share;
std::string pool_username;
std::string pool_password;

/*********************************
* helping functions
*********************************/

void convertDataToBlock(unsigned char* blockData, CBlock& block) {
  {
    std::stringstream ss;
    for (int i = 7; i >= 0; --i)
		ss << std::setw(8) << std::setfill('0') << std::hex << *((int *)(blockData + 4) + i);
    ss.flush();
    block.hashPrevBlock.SetHex(ss.str().c_str());
  }
  {
    std::stringstream ss;
    for (int i = 7; i >= 0; --i)
		ss << std::setw(8) << std::setfill('0') << std::hex << *((int *)(blockData + 36) + i);
    ss.flush();
    block.hashMerkleRoot.SetHex(ss.str().c_str());
  }
  block.nVersion               = *((int *)(blockData));
  block.nTime                  = *((unsigned int *)(blockData + 68));
  block.nBits                  = *((unsigned int *)(blockData + 72));
  block.nNonce                 = *((unsigned int *)(blockData + 76));
  block.nBirthdayA = 0;
  block.nBirthdayB = 0;
}

uint256 hexToHash(std::string hex) {
	CBigNum n;
	n.setvch(ParseHex(hex));
	return n.getuint256();
}

/*********************************
* class CBlockProviderGW to (incl. SUBMIT_BLOCK)
*********************************/

class CBlockProviderGW : public CBlockProvider {
public:

	CBlockProviderGW() : CBlockProvider(), nTime_offset(0), _blocks(NULL) {}

	virtual ~CBlockProviderGW() { /* TODO */ }
	
	virtual unsigned int GetAdjustedTimeWithOffset(unsigned int thread_id) {
		return nTime_offset + ((((unsigned int)GetAdjustedTime() + thread_num_max) / thread_num_max) * thread_num_max) + thread_id;
	}
	
	virtual CBlock* getOriginalBlock() {
		return _blocks;
	}

	virtual CBlock* getBlock(unsigned int thread_id, unsigned int last_time, unsigned int counter) {
		boost::unique_lock<boost::shared_mutex> lock(_mutex_getwork);
		if (_blocks == NULL) return NULL;
		CBlock* block = NULL;
		block = new CBlock(_blocks->GetBlockHeader());
		
		// Create merkle root
		int en2 = rand(); // TODO: Save en2 somewhere
		std::string extranonce2 = HexStr(BEGIN(en2), END(en2));
		std::string cbtxn = coinb1 + extranonce1 + extranonce2 + coinb2;
		std::cout << "CBTXN: " << cbtxn << std::endl;
		std::vector<unsigned char> coinbase = ParseHex(cbtxn);
		uint256 cbHash = Hash(BEGIN(coinbase[0]), END(coinbase[coinbase.size() - 1]));
		std::cout << "CBHASH: " << cbHash.GetHex() << std::endl;
		
		unsigned char mr[32];
		//memcpy(&mr[0], cbHash.begin(), 32); // coinbase = base for merkle root
		/*for(unsigned int i = 0;i < 32;i++) {
			mr[i] = *(cbHash.begin() + 31 - i);
		}*/
		
		//std::cout << "CBHREV: " << cbHash.GetHex() << std::endl;
		
		// Time for some fun memory operations
		unsigned char t[64];
		memcpy(&t[0], cbHash.begin(), 32);
		//memcpy(&t[0], cbHash.begin(), 32); // coinbase = base for merkle root
		for(unsigned int i = 0;i < merkle_branch.size();i++) {
			memcpy(&t[32], &ParseHex(merkle_branch[i])[0], 32);
			//std::cout << "MBHash: " << t[1].GetHex() << std::endl;
			memcpy(&t[0], Hash(BEGIN(t[0]), END(t[63])).begin(), 32);
		}
		
		// Reverse the hash
		//unsigned char mr[32];
		/*for(unsigned int i = 0;i < 32;i++) {
			mr[i] = t[31 - i];
		}*/
		
		// We should now have our merkle root hash in t[0] - t[31]
		memcpy(block->hashMerkleRoot.begin(), &t[0], 32);
		
		std::cout << "MERKLE ROOT: " << block->hashMerkleRoot.GetHex() << std::endl;
		std::cout << "EN2: " << extranonce2 << std::endl;
		
		merkles[block->hashMerkleRoot] = extranonce2;
		
		unsigned int new_time = GetAdjustedTimeWithOffset(thread_id);
		new_time += counter * thread_num_max;
		block->nTime = new_time; //TODO: check if this is the same time like before!?
		return block;
	}
	
	void setExtraNonce2Len(int len) {
		extranonce2_size = len;
	}
	
	void setExtraNonce1(std::string len) {
		extranonce1 = len;
	}
	
	// recieves direct params from mining.notify and gets jobs going
	void setBlocksFromData(json_spirit::mArray params) {
		job_id = params[0].get_str();
		CBlock* block = new CBlock();
		block->hashPrevBlock = hexToHash(params[1].get_str());
		std::cout << "PREVHASH: " << block->hashPrevBlock.GetHex() << std::endl;
		coinb1 = params[2].get_str();
		coinb2 = params[3].get_str();
		
		std::cout << "COINB1: " << coinb1 << std::endl;
		std::cout << "COINB2: " << coinb2 << std::endl;
		
		json_spirit::mArray mb = params[4].get_array();
		merkle_branch.clear();
		for(unsigned int i = 0;i < mb.size();i++) merkle_branch.push_back(mb[i].get_str());
		// Cheat way to turn hex to normal int
		CBigNum c;
		c.setvch(ParseHex(params[5].get_str()));
		block->nVersion = c.getint();
		std::cout << "VERSION: " << block->nVersion << std::endl;
		c.setvch(ParseHex(params[6].get_str()));
		block->nBits = c.getint();
		std::cout << "BITS: " << block->nBits << std::endl;
		c.setvch(ParseHex(params[7].get_str()));
		block->nTime = c.getint();
		std::cout << "TIME: " << block->nTime << std::endl;
		block->nNonce = 0;
		block->nBirthdayA = 0;
		block->nBirthdayB = 0;
		
		unsigned int nTime_local = GetAdjustedTime();
		unsigned int nTime_server = block->nTime;
		nTime_offset = nTime_local > nTime_server ? 0 : (nTime_server-nTime_local);
		
		CBlock* old_blocks = NULL;
		{
			boost::unique_lock<boost::shared_mutex> lock(_mutex_getwork);
			old_blocks = _blocks;
			_blocks = block;
		}
		if (old_blocks != NULL) delete old_blocks;
		
		merkles.clear();
	}

	void submitBlock(CBlock *block) {

		std::cout << "submit: " << block->GetHash().GetHex() << std::endl;
		
		std::string extranonce2 = merkles[block->hashMerkleRoot]; // Should 
		if(extranonce2 == "") extranonce2 = "ffffffff"; // So that we dont crash
		
		std::stringstream ss;
		ss << "{\"params\": [\"" << pool_username << "\", \"" << job_id + "\", \"" << extranonce2 << "\", \"" << HexStr(BEGIN(block->nTime), END(block->nTime)) << "\", \"" << HexStr(BEGIN(block->nNonce), END(block->nNonce)) << "\", \"" << HexStr(BEGIN(block->nBirthdayA), END(block->nBirthdayA)) << "\", \"" << HexStr(BEGIN(block->nBirthdayB), END(block->nBirthdayB)) << "\"], \"id\": " << (rand() % 999000 + 1000) << ", \"method\": \"mining.submit\"}\n";
		std::string submit = ss.str();
		
		std::cout << submit << std::endl;
		
		boost::system::error_code error;
		boost::asio::write(*socket_to_server, boost::asio::buffer(submit.c_str(), submit.size()));
	}

	void forceReconnect() {
		std::cout << "force reconnect if possible!" << std::endl;
		if (socket_to_server != NULL) {
			boost::system::error_code close_error;
			socket_to_server->close(close_error);
			//if (close_error)
			//	std::cout << close_error << " @ close" << std::endl;
		}
	}

protected:
	std::string job_id;
	std::string coinb1;
	std::string extranonce1;
	std::string coinb2;
	int extranonce2_size;
	
	std::map<uint256, std::string> merkles;
	
	std::vector<std::string> merkle_branch;
	
	
	unsigned int nTime_offset;
	boost::shared_mutex _mutex_getwork;
	CBlock* _blocks;
};

/*********************************
* multi-threading
*********************************/

class CMasterThreadStub {
public:
  virtual void wait_for_master() = 0;
  virtual boost::shared_mutex& get_working_lock() = 0;
};

class CWorkerThread { // worker=miner
public:

	CWorkerThread(CMasterThreadStub *master, unsigned int id, CBlockProviderGW *bprovider)
		: _working_lock(NULL), _id(id), _master(master), _bprovider(bprovider), _thread(&CWorkerThread::run, this) { }

	void run() {
		std::cout << "[WORKER" << _id << "] Hello, World!" << std::endl;
		_master->wait_for_master();
		std::cout << "[WORKER" << _id << "] GoGoGo!" << std::endl;
		boost::this_thread::sleep(boost::posix_time::seconds(2));
		BitcoinMiner(NULL, _bprovider, _id);
		std::cout << "[WORKER" << _id << "] Bye Bye!" << std::endl;
	}

	void work() { // called from within master thread
		_working_lock = new boost::shared_lock<boost::shared_mutex>(_master->get_working_lock());
	}

protected:
  boost::shared_lock<boost::shared_mutex> *_working_lock;
  unsigned int _id;
  CMasterThreadStub *_master;
  CBlockProviderGW  *_bprovider;
  boost::thread _thread;
};

class CMasterThread : public CMasterThreadStub {
public:

  CMasterThread(CBlockProviderGW *bprovider) : CMasterThreadStub(), _bprovider(bprovider) {}

  void run() {

	{
		boost::unique_lock<boost::shared_mutex> lock(_mutex_master);
		std::cout << "spawning the worker" << std::endl;
		
		nThreads = thread_num_max;

		//for (unsigned int i = 0; i < thread_num_max; ++i) {
		CWorkerThread *worker = new CWorkerThread(this, 0, _bprovider);
		worker->work();
		//}
	}

    boost::asio::io_service io_service;
    boost::asio::ip::tcp::resolver resolver(io_service); //resolve dns
    boost::asio::ip::tcp::resolver::query query(GetArg("-poolip", "127.0.0.1"), GetArg("-poolport", "1337"));
    boost::asio::ip::tcp::resolver::iterator endpoint;
	boost::asio::ip::tcp::resolver::iterator end;
	boost::asio::ip::tcp::no_delay nd_option(true);
	boost::asio::socket_base::keep_alive ka_option(true);

	while (running) {
		endpoint = resolver.resolve(query);
		boost::scoped_ptr<boost::asio::ip::tcp::socket> socket;
		boost::system::error_code error_socket = boost::asio::error::host_not_found;
		while (error_socket && endpoint != end)
		{
		  //socket->close();
		  socket.reset(new boost::asio::ip::tcp::socket(io_service));
		  boost::asio::ip::tcp::endpoint tcp_ep = *endpoint++;
		  socket->connect(tcp_ep, error_socket);
		  std::cout << "connecting to " << tcp_ep << std::endl;
		}
		socket->set_option(nd_option);
		socket->set_option(ka_option);

		if (error_socket) {
			std::cout << error_socket << std::endl;
			boost::this_thread::sleep(boost::posix_time::seconds(10));
			continue;
		}

		{ //send hello message
			
			// Really lazy, improper way to send hello request
			std::string req = "{\"id\": 1, \"method\": \"mining.subscribe\", \"params\": []}\n{\"params\": [\"" + pool_username + "\", \"" + pool_password + "\"], \"id\": 2, \"method\": \"mining.authorize\"}\n";
			boost::asio::write(*socket, boost::asio::buffer(req.c_str(), strlen(req.c_str())));
		}

		socket_to_server = socket.get(); //TODO: lock/mutex

		int reject_counter = 0;
		bool done = false;
		
		
		boost::asio::streambuf b;
		
		while (!done) {
			
			{
				boost::system::error_code error;
				boost::asio::read_until(*socket_to_server, b, "\n", error);
				if (error == boost::asio::error::eof)
					break; // Connection closed cleanly by peer.
				else if (error) {
					std::cout << error << " @ read_some1" << std::endl;
					break;
				}
				
			}
			
			std::istream is(&b);
			
			std::string req;
			
			getline(is, req);
			
			try {
				// Parse with JSON
				json_spirit::mValue v;
				if(json_spirit::read(req, v)) {
					json_spirit::mObject obj = v.get_obj();
					if(obj.count("method") > 0) {
						std::string method = obj["method"].get_str();
						json_spirit::mArray params = obj["params"].get_array();
						
						if(method == "mining.set_difficulty") {
							double difficulty = params[0].get_real();
							
							long d = 1 / difficulty;
							
							CBigNum target;
							target.SetCompact(0x1d00ffff);
							target = target * d;
							
							bnPoolTarget = target.getuint256();
							
							std::cout << "Setting difficulty to " << bnPoolTarget.GetHex() << std::endl;
						}
						else if(method == "mining.notify") {
							_bprovider->setBlocksFromData(params);
							std::cout << "Work recieved!" << std::endl;
						}
						
						// More lazy protocol implementation	
						std::stringstream ss;
						ss << "{\"error\": null, \"id\": " << obj["id"].get_int() << ", \"result\": null}\n";
						std::string res = ss.str();
						boost::asio::write(*socket, boost::asio::buffer(res.c_str(), strlen(res.c_str())));
						
					}
					else {
						// Result of an operation
						
						// Subscribe result
						int id = obj["id"].get_int();
						json_spirit::mValue res = obj["result"];
						if(id == 1) {
							json_spirit::mArray arr = res.get_array();
							_bprovider->setExtraNonce1(arr[1].get_str());
							_bprovider->setExtraNonce2Len(arr[2].get_int());
							std::cout << "Subscribed for work" << std::endl;
						}
						
						// Authentication result
						else if(id == 2) {
							bool worked = res.get_bool();
							if(worked) std::cout << "Successfully logged in!" << std::endl;
							else std::cout << "Error logging in! Check details and try again!";
						}
						
						// Share submit result
						else if(id >= 1000 && id < 1000000) {
							bool worked  = res.get_bool();
							if(worked) {
								
							}
							else {
								std::cout << "Share submission failed!" << std::endl;
								reject_counter++;
							}
							stats_running();
						}
					}
				}
				else std::cout << "JSON Parse Error from Server: " << req << std::endl;
			} catch(std::exception e) {
				std::cout << "Exception while processing JSON from pool!" << std::endl;
			}
		}

		socket_to_server = NULL; //TODO: lock/mutex
		for (int i = 0; i < 50 && submitting_share < 1; ++i) //wait <5 seconds until reconnect (force reconnect when share is waiting to be submitted)
			boost::this_thread::sleep(boost::posix_time::milliseconds(100));
	}
  }

  ~CMasterThread() {}

  void wait_for_master() {
    boost::shared_lock<boost::shared_mutex> lock(_mutex_master);
  }

  boost::shared_mutex& get_working_lock() {
    return _mutex_working;
  }

private:

  void wait_for_workers() {
    boost::unique_lock<boost::shared_mutex> lock(_mutex_working);
  }

  CBlockProviderGW  *_bprovider;

  boost::shared_mutex _mutex_master;
  boost::shared_mutex _mutex_working;

	// Provides real time stats
	void stats_running() {
		if (!running) return;
		std::cout << std::fixed;
		std::cout << std::setprecision(1);
		boost::posix_time::ptime t_end = boost::posix_time::second_clock::universal_time();
		unsigned long rejects = 0;
		unsigned long stale = 0;
		unsigned long valid = 0;
		unsigned long blocks = 0;
		for (std::map<int,unsigned long>::iterator it = statistics.begin(); it != statistics.end(); ++it) {
			if (it->first < 0) stale += it->second;
			if (it->first == 0) rejects = it->second;
			if (it->first == 1) blocks = it->second;
			if (it->first > 1) valid += it->second;
		}
		std::cout << "[STATS] " << DateTimeStrFormat("%Y-%m-%d %H:%M:%S", GetTimeMillis() / 1000).c_str() << " | ";
		for (std::map<int,unsigned long>::iterator it = statistics.begin(); it != statistics.end(); ++it)
			if (it->first > 1)
				std::cout << "SHARES: " << it->second << " (" <<
				  ((valid+blocks > 0) ? (static_cast<double>(it->second) / static_cast<double>(valid+blocks)) * 100.0 : 0.0) << "% | " <<
				  ((valid+blocks > 0) ? (static_cast<double>(it->second) / (static_cast<double>((t_end - t_start).total_seconds()) / 3600.0)) : 0.0) << "/h), ";
		if (valid+blocks+rejects+stale > 0) {
		std::cout << "VL: " << valid+blocks << " (" << (static_cast<double>(valid+blocks) / static_cast<double>(valid+blocks+rejects+stale)) * 100.0 << "%), ";
		std::cout << "RJ: " << rejects << " (" << (static_cast<double>(rejects) / static_cast<double>(valid+blocks+rejects+stale)) * 100.0 << "%), ";
		std::cout << "ST: " << stale << " (" << (static_cast<double>(stale) / static_cast<double>(valid+blocks+rejects+stale)) * 100.0 << "%)" << std::endl;
		} else {
			std::cout <<  "VL: " << 0 << " (" << 0.0 << "%), ";
			std::cout <<  "RJ: " << 0 << " (" << 0.0 << "%), ";
			std::cout <<  "ST: " << 0 << " (" << 0.0 << "%)" << std::endl;
		}
	}
};

/*********************************
* exit / end / shutdown
*********************************/

void stats_on_exit() {
	if (!running) return;
	boost::this_thread::sleep(boost::posix_time::seconds(1));
	std::cout << std::fixed;
	std::cout << std::setprecision(3);
	boost::posix_time::ptime t_end = boost::posix_time::second_clock::universal_time();
	unsigned long rejects = 0;
	unsigned long stale = 0;
	unsigned long valid = 0;
	unsigned long blocks = 0;
	for (std::map<int,unsigned long>::iterator it = statistics.begin(); it != statistics.end(); ++it) {
		if (it->first < 0) stale += it->second;
		if (it->first == 0) rejects = it->second;
		if (it->first == 1) blocks = it->second;
		if (it->first > 1) valid += it->second;
	}
	std::cout << std::endl;
	std::cout << "********************************************" << std::endl;
	std::cout << "*** running time: " << static_cast<double>((t_end - t_start).total_seconds()) / 3600.0 << "h" << std::endl;
	std::cout << "***" << std::endl;
	for (std::map<int,unsigned long>::iterator it = statistics.begin(); it != statistics.end(); ++it)
		if (it->first > 1)
			std::cout << "*** " << it->first << "-chains: " << it->second << "\t(" <<
			  ((valid+blocks > 0) ? (static_cast<double>(it->second) / static_cast<double>(valid+blocks)) * 100.0 : 0.0) << "% | " <<
			  ((valid+blocks > 0) ? (static_cast<double>(it->second) / (static_cast<double>((t_end - t_start).total_seconds()) / 3600.0)) : 0.0) << "/h)" <<
			  std::endl;
	if (valid+blocks+rejects+stale > 0) {
	std::cout << "***" << std::endl;
	std::cout << "*** valid: " << valid+blocks << "\t(" << (static_cast<double>(valid+blocks) / static_cast<double>(valid+blocks+rejects+stale)) * 100.0 << "%)" << std::endl;
	std::cout << "*** rejects: " << rejects << "\t(" << (static_cast<double>(rejects) / static_cast<double>(valid+blocks+rejects+stale)) * 100.0 << "%)" << std::endl;
	std::cout << "*** stale: " << stale << "\t(" << (static_cast<double>(stale) / static_cast<double>(valid+blocks+rejects+stale)) * 100.0 << "%)" << std::endl;
	} else {
		std::cout <<  "*** valid: " << 0 << "\t(" << 0.0 << "%)" << std::endl;
		std::cout <<  "*** rejects: " << 0 << "\t(" << 0.0 << "%)" << std::endl;
		std::cout <<  "*** stale: " << 0 << "\t(" << 0.0 << "%)" << std::endl;
	}
	std::cout << "********************************************" << std::endl;
	boost::this_thread::sleep(boost::posix_time::seconds(3));
}

void exit_handler() {
	//cleanup for not-retarded OS
	if (socket_to_server != NULL) {
		socket_to_server->close();
		socket_to_server = NULL;
	}
	stats_on_exit();
	running = false;
}

#if defined(__MINGW32__) || defined(__MINGW64__)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

BOOL WINAPI ctrl_handler(DWORD dwCtrlType) {
	//'special' cleanup for windows
	switch(dwCtrlType) {
		case CTRL_C_EVENT:
		case CTRL_BREAK_EVENT: {
			if (socket_to_server != NULL) {
				socket_to_server->close();
				socket_to_server = NULL;
			}
			stats_on_exit();
			running = false;
		} break;
		default: break;
	}
	return FALSE;
}

#elif defined(__GNUG__)

static sighandler_t set_signal_handler (int signum, sighandler_t signalhandler) {
   struct sigaction new_sig, old_sig;
   new_sig.sa_handler = signalhandler;
   sigemptyset (&new_sig.sa_mask);
   new_sig.sa_flags = SA_RESTART;
   if (sigaction (signum, &new_sig, &old_sig) < 0)
      return SIG_ERR;
   return old_sig.sa_handler;
}

void ctrl_handler(int signum) {
	exit(1);
}

#endif //TODO: __APPLE__ ?

/*********************************
* main - this is where it begins
*********************************/
int main(int argc, char **argv)
{
  std::cout << "********************************************" << std::endl;
  std::cout << "*** Stratum Memorycoin Pool Miner v" << VERSION_MAJOR << "." << VERSION_MINOR << " " << VERSION_EXT << std::endl;
  std::cout << "*** by KillerByte  - CNO of Memorycoin and Co-Admin of XRam Pools" << std::endl;
  std::cout << "***" << std::endl;
  std::cout << "*** Thanks to xolokram (www.beeeeer.org)" << std::endl;
  std::cout << "*** press CTRL+C to exit" << std::endl;
  std::cout << "********************************************" << std::endl;

  t_start = boost::posix_time::second_clock::universal_time();
  running = true;

#if defined(__MINGW32__) || defined(__MINGW64__)
  SetConsoleCtrlHandler(ctrl_handler, TRUE);
#elif defined(__GNUG__)
  set_signal_handler(SIGINT, ctrl_handler);
#endif //TODO: __APPLE__

  if (argc < 2)
  {
    std::cerr << "usage: " << argv[0] <<
    " -poolfee=<fee-in-%> -poolip=<ip> -poolport=<port> -pooluser=<user> -poolpassword=<password>" <<
    std::endl;
    return EXIT_FAILURE;
  }

  const int atexit_res = std::atexit(exit_handler);
  if (atexit_res != 0)
    std::cerr << "atexit registration failed, shutdown will be dirty!" << std::endl;

  // init everything:
  ParseParameters(argc, argv);

  socket_to_server = NULL;
  //pool_share_minimum = (unsigned int)GetArg("-poolshare", 7);
  thread_num_max = GetArg("-genproclimit", 1); // what about boost's hardware_concurrency() ?
  //fee_to_pay = GetArg("-poolfee", 3);
  miner_id = GetArg("-minerid", 0);
  pool_username = GetArg("-pooluser", "");
  pool_password = GetArg("-poolpassword", "");

  if (thread_num_max == 0 || thread_num_max > MAX_THREADS)
  {
    std::cerr << "usage: " << "current maximum supported number of threads = " << MAX_THREADS << std::endl;
    return EXIT_FAILURE;
  }

  /*if (fee_to_pay == 0 || fee_to_pay > 100)
  {
    std::cerr << "usage: " << "please use a pool fee between [1 , 100]" << std::endl;
    return EXIT_FAILURE;
  }*/

  if (miner_id > 65535)
  {
    std::cerr << "usage: " << "please use a miner id between [0 , 65535]" << std::endl;
    return EXIT_FAILURE;
  }
  
  { //password to sha1
    boost::uuids::detail::sha1 sha;
    sha.process_bytes(pool_password.c_str(), pool_password.size());
    unsigned int digest[5];
    sha.get_digest(digest);
    std::stringstream ss;
    ss << std::setw(5) << std::setfill('0') << std::hex << (digest[0] ^ digest[1] ^ digest[4]) << (digest[2] ^ digest[3] ^ digest[4]);
    pool_password = ss.str();
  }
std::cout << pool_username << std::endl;

  fPrintToConsole = true; // always on
  fDebug          = GetBoolArg("-debug");

  pindexBest = new CBlockIndex();

  //GeneratePrimeTable();

  // ok, start mining:
  CBlockProviderGW* bprovider = new CBlockProviderGW();
  CMasterThread *mt = new CMasterThread(bprovider);
  mt->run();

  // end:
  return EXIT_SUCCESS;
}

//#include "main_poolminer_ex.cpp" //<--TODO

/*********************************
* and this is where it ends
*********************************/
