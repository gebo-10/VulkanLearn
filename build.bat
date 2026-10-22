@REM xmake install_depends
@REM git clone --depth=1  https://github.com/gebo-10/Assets.git assets
git clone https://github.com/gebo-10/Klib.git external/klib
xmake f --runtimes=MDd --vs=2022 --mode=debug
xmake

@REM xmake project -k vsxmake

@REM 代理
@REM $env:http_proxy = "http://127.0.0.1:1081"
@REM $env:https_proxy = "http://127.0.0.1:1081"
@REM $env:ALL_PROXY = "socks5://127.0.0.1:1081"