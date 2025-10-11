source /home/rnd/STM32MP157F-DK2_Starter/environment-setup-cortexa7t2hf-neon-vfpv4-ostl-linux-gnueabi

export CROSS_TOOLCHAIN_CMAKE_FILE=/home/rnd/STM32MP157F-DK2_Starter/sysroots/x86_64-ostl_sdk-linux/usr/share/cmake/OEToolchainConfig.cmake
export CMAKE_SYSROOT=/home/rnd/STM32MP157F-DK2_Starter/sysroots/cortexa7t2hf-neon-vfpv4-ostl-linux-gnueabi

echo $CROSS_TOOLCHAIN_CMAKE_FILE
echo $CMAKE_SYSROOT
which arm-ostl-linux-gnueabi-gcc

rm -rf build
mkdir build
cd build

cmake .. \
  -DCMAKE_TOOLCHAIN_FILE="$CROSS_TOOLCHAIN_CMAKE_FILE" \
  -DCMAKE_SYSROOT="$CMAKE_SYSROOT" \
  -DQt6_DIR="/usr/lib/x86_64-linux-gnu/cmake/Qt6" \
  -DQt6HostInfo_DIR="/usr/lib/x86_64-linux-gnu/cmake/Qt6HostInfo" \
  -DCMAKE_BUILD_TYPE=Release

make -j$(nproc)


