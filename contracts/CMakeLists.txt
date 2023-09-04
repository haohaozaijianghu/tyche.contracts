cmake_minimum_required( VERSION 3.5 )

project(contracts)

set(AMAX_WASM_OLD_BEHAVIOR "Off")
find_package(amax.cdt)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} $CACHE{CMAKE_CXX_FLAGS}")

set(ICON_BASE_URL "http://127.0.0.1/ricardian_assets/entu.contracts/icons")

set(ACCOUNT_ICON_URI  "account.png#3d55a2fc3a5c20b456f5657faf666bc25ffd06f4836c5e8256f741149b0b294f")
set(ADMIN_ICON_URI    "admin.png#9bf1cec664863bd6aaac0f814b235f8799fb02c850e9aa5da34e8a004bd6518e")
set(TRANSFER_ICON_URI "transfer.png#5dfad0df72772ee1ccc155e670c1d124f5c5122f1d5027565df38b418042d1dd")

# add_subdirectory(entu.token)
# add_subdirectory(entu.ido)
# add_subdirectory(entu.swap)
# add_subdirectory(entu.reward)
# add_subdirectory(entu.burnpool)
 add_subdirectory(usdt.save)
 add_subdirectory(usdt.interest)
