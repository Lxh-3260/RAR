# RAR
## ENV
Ubuntu		20.04
gcc			5.3.1
g++			5.3.1
python		2.7.18 
scons		3.0.5
swig		3.0.12
scons		3.0.5
RTSim		latest version
Gem5		Versions before 2018
SPEC2006	v1.1

Because GEM5 stopped maintaining and updating its support for the NVMain version after 2018, the latest version of GEM5 on the official website or GitHub does not support mixed compilation with NVMain (and its derivative RTSim). When applying the latest version of NVMain or RTSim with latest update on gem5, it will occur error during compiling. The specific solution is shown in this [issue](https://github.com/SEAL-UCSB/NVmain/issues/4#issuecomment-954666264) commented by seceng-jan. I was also inspired by the [youtube video](https://www.youtube.com/watch?v=Udk7GKl9GcI), which presents the entire procedure of intergration of Gem5 and NVMain.

## Directory Structure
```shell
*
├── cpu2006-v1.1
│
├── Gem5
│
├── RTSim
```

## integrate Gem5 and RTSim
```shell
# run in RTSim directory
scons --build-type=fast -j 12
# run in Gem5 directory
scons build/X86/gem5.opt -j 4
git apply ../RTSim/patches/gem5/nvmain2-gem5-11688+
scons EXTRAS=../RTSim build/X86/gem5.opt -j <No. of Threads in your CPU>
./build/X86/gem5.opt configs/example/se.py -c tests/test-progs/hello/bin/x86/linux/hello --caches --l2cache --mem-type=NVMainMemory --nvmain-config=../RTsim/Config/RM.config
```
if --mem-type can select NVMainMemory, it is integrated successfully.

## test SPEC2006
```shell
# compile benchmark
runspec --config=gcc-2006-Ofast.cfg --action=build --tune=base <which benchmark you select>
# test benchmark
runspec --config=gcc-2006-Ofast.cfg --size=ref -I --tune=base <which benchmark you select>
# run in Gem5 directory
sudo ./run_gem5_amd64_spec06_benchmarks.sh <which benchmark you select> /home/lixinghao/Gem5/m5out/spec
```