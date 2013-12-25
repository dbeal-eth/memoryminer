
file(GLOB BITCOIN_TS_FILES *.ts)

# Generate intermediate Qt-specific files from .ui and .h
qt4_add_translation(BITCOIN_QM_FILES ${BITCOIN_TS_FILES})
add_custom_target(memorycoin_translations DEPENDS ${BITCOIN_QM_FILES})
set(BITCOIN_QT_TRANSLATIONS ${QT_TRANSLATIONS})


