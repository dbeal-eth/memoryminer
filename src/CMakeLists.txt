set(DAEMON_SOURCES
	main.cpp
	alert.cpp
	version.cpp
	sync.cpp
	util.cpp
	hash.cpp
	netbase.cpp
	key.cpp
	script.cpp
	init.cpp
	net.cpp
	bloom.cpp
	checkpoints.cpp
	addrman.cpp
	db.cpp
	walletdb.cpp
	wallet.cpp
	keystore.cpp
	momentum.cpp
	bitcoinrpc.cpp
	rpcdump.cpp
	rpcnet.cpp
	rpcmining.cpp
	rpcwallet.cpp
	rpcblockchain.cpp
	rpcrawtransaction.cpp
	crypter.cpp
	protocol.cpp
	noui.cpp
	leveldb.cpp
	txdb.cpp
	)

add_subdirectory(leveldb)

add_definitions( "-DHAS_BOOST -DHAVE_WORKING_BOOST_SLEEP" )

if(BUILD_QT)
	add_definitions( "-DQT_GUI")
endif()

include_directories(${BDB_INCLUDE}
                    ${Boost_INCLUDE_DIRS}
                    ${OPENSSL_INCLUDE_DIRS}
                    "leveldb/include"
                    "leveldb/helpers"
                    )
add_library(memorycoin ${DAEMON_SOURCES})
target_link_libraries(memorycoin leveldb
		${CMAKE_THREAD_LIBS_INIT}
		${BDB_LIBRARY}
		${Boost_LIBRARIES}
		${OPENSSL_LIBRARIES}
		)
add_executable(memorycoind main.cpp)
target_link_libraries(memorycoind memorycoin)

if(BUILD_QT)
    	add_subdirectory( qt )
else(BUILD_QT)
	add_executable(memorycoind main.cpp)
	target_link_libraries(memorycoind memorycoin)
endif(BUILD_QT)

#add_subdirectory(test)

