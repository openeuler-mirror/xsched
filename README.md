# XSched：面向多样化XPU的抢占式调度

[![License](https://img.shields.io/badge/License-Apache_2.0-blue)](https://github.com/XpuOS/xsched/blob/main/LICENSE)


---

## 最新动态

- [2025/08] 我们将XSched迁移到了Windows，并完成了在CUDA、LevelZero和OpenCL上的验证。
- [2025/07] 我们将XSched集成到了[llama.cpp](integration/llama.cpp)和[NVIDIA Triton](integration/triton)中，以支持多个推理请求之间基于优先级的调度。
- [2025/07] 我们在[OSDI 2025](https://www.usenix.org/conference/osdi25/presentation/shen-weihang)上展示了XSched论文，并准备了几个有趣的演示视频：[Ascend910](https://github.com/user-attachments/assets/bc668f4d-33d9-4492-9900-8c3b10fdd1af)，[GV100](https://github.com/user-attachments/assets/dffb821b-92e2-44c5-bd59-a6946b0c4d02)，[Least-Laxity-First](https://github.com/user-attachments/assets/885886e1-1920-4fb1-aa2d-50f4f88cf660)和[Active-Window-First](https://github.com/user-attachments/assets/877aeb5f-35b6-4bc1-b553-d76525a8adb3)。
- [2025/06] 我们正式发布了XSched！请查看我们的[博客文章](docs/xsched-intro-2025-en.md)。


---


## 演示
XSched消除了AI PC上AI视频会议应用的视频卡顿现象

- 硬件：[Intel Core Ultra NPU](https://www.intel.com/content/www/us/en/products/details/processors/core-ultra.html)(即 NPU 3720)
- 工作负载：虚拟背景网络摄像头([LFBW](https://github.com/fangfufu/Linux-Fake-Background-Webcam))和语音转文本([whisper.cpp](https://github.com/ggml-org/whisper.cpp))
- 调度策略：[least-laxity-first](https://ieeexplore.ieee.org/document/726348)策略的一个变体

更多详情请参阅我们[论文](docs/xsched-osdi25.pdf)中的第2个案例研究。

https://github.com/user-attachments/assets/3eb256c3-9107-4d8b-ae8d-0e3ada54aec1


## 简介

XSched是一个面向跨品牌、跨代际和跨软件平台的多样化XPU（指各种加速器，如 GPU、NPU、ASIC和FPGA）的抢占式调度框架。XSched通过可抢占的命令队列抽象（XQueue）为调度XPU任务提供统一接口，从而实现针对不同目标的硬件无关且灵活的调度策略。XSched引入了多级硬件模型，有助于先进的XPU实现最佳调度性能，同时保持与新兴XPU的兼容性。该框架旨在高效地调度XPU任务，同时对现有的基于XPU的应用程序保持透明。


### 特性

- **透明性**：无需修改代码即可在现有应用程序上运行。
- **灵活性**：支持多样化的调度策略和加速器类型。
- **扩展性**：适应新的硬件特性和软件平台。
- **集成性**：适应操作系统级和系统级多任务处理场景。
- **高性能**：以极小的运行时开销提供高效的调度性能。


## XPU 支持列表

✅ 已支持并已实现

❌ 不支持

🔘 尚未实现

🚧 正在实现中

<table>
  <tr>
    <th align="center">Platform</th>
    <th align="center">XPU</th>
    <th align="center">Shim</th>
    <th align="center">Level-1</th>
    <th align="center">Level-2</th>
    <th align="center">Level-3</th>
  </tr>
  <tr>
    <td align="center" rowspan="4"><a href="platforms/cuda">CUDA</a></td>
    <td align="center">NVIDIA Ampere GPUs (sm86)</td>
    <td align="center" rowspan="4">✅</td>
    <td align="center" rowspan="4">✅</td>
    <td align="center">🚧</td>
    <td align="center">🚧</td>
  </tr>
  <tr>
    <td align="center">NVIDIA Volta GPUs (sm70)</td>
    <td align="center">✅</td>
    <td align="center">✅</td>
  </tr>
  <tr>
    <td align="center">NVIDIA Kepler GPUs (sm35)</td>
    <td align="center">✅</td>
    <td align="center">❌</td>
  </tr>
  <tr>
    <td align="center">Other NVIDIA GPUs</td>
    <td align="center">🔘</td>
    <td align="center">🔘</td>
  </tr>
  <tr>
    <td align="center" rowspan="1"><a href="platforms/hip">HIP</a></td>
    <td align="center">AMD GPUs</td>
    <td align="center" rowspan="1">✅</td>
    <td align="center" rowspan="1">✅</td>
    <td align="center">🔘</td>
    <td align="center">🔘</td>
  </tr>
  <tr>
    <td align="center" rowspan="2"><a href="platforms/levelzero">LevelZero</a></td>
    <td align="center">Intel GPUs</td>
    <td align="center" rowspan="2">✅</td>
    <td align="center" rowspan="2">✅</td>
    <td align="center">🔘</td>
    <td align="center">🔘</td>
  </tr>
  <tr>
    <td align="center">Intel Integrated NPUs</td>
    <td align="center">✅</td>
    <td align="center">❌</td>
  </tr>
  <tr>
    <td align="center" rowspan="4"><a href="platforms/opencl">OpenCL</a></td>
    <td align="center">NVIDIA GPUs</td>
    <td align="center" rowspan="4">✅</td>
    <td align="center" rowspan="4">✅</td>
    <td align="center">🔘</td>
    <td align="center">🔘</td>
  </tr>
  <tr>
    <td align="center">AMD GPUs</td>
    <td align="center">🔘</td>
    <td align="center">🔘</td>
  </tr>
  <tr>
    <td align="center">Intel GPUs</td>
    <td align="center">🔘</td>
    <td align="center">🔘</td>
  </tr>
  <tr>
    <td align="center">Xilinx FPGAs</td>
    <td align="center">🔘</td>
    <td align="center">❌</td>
  </tr>
  <tr>
    <td align="center" rowspan="1"><a href="platforms/ascend">AscendCL</a></td>
    <td align="center">Ascend NPUs</td>
    <td align="center" rowspan="1">✅</td>
    <td align="center" rowspan="1">✅</td>
    <td align="center">🔘</td>
    <td align="center">❌</td>
  </tr>
  <tr>
    <td align="center" rowspan="1"><a href="platforms/cudla">cuDLA</a></td>
    <td align="center">NVIDIA DLA</td>
    <td align="center" rowspan="1">✅</td>
    <td align="center" rowspan="1">✅</td>
    <td align="center">❌</td>
    <td align="center">❌</td>
  </tr>
  <tr>
    <td align="center" rowspan="2"><a href="platforms/vpi">VPI</a></td>
    <td align="center">NVIDIA OFA</td>
    <td align="center" rowspan="2">✅</td>
    <td align="center" rowspan="2">✅</td>
    <td align="center">❌</td>
    <td align="center">❌</td>
  </tr>
  <tr>
    <td align="center">NVIDIA PVA</td>
    <td align="center">❌</td>
    <td align="center">❌</td>
  </tr>
</table>


## 快速开始

### 构建和安装XSched

#### 克隆仓库

```bash
git clone https://github.com/XpuOS/xsched.git
cd xsched
git submodule update --init --recursive
```

#### 构建和安装

```bash
# 仅构建XSched
# 默认情况下,XSched将安装到xsched/output目录
make
# 将XSched安装到自定义路径
make INSTALL_PATH=/path/to/install

# 构建XSched及其特定于平台的组件（HAL,Shim等）
make cuda # 或hip,levelzero,opencl,ascend,cudla,vpi
# 或者指定PLATFORM变量
make PLATFORM = cuda
make PLATFORM = "cuda levelzero opencl" # 一次性构建多个平台
```


### 使用案例

#### 透明地调度应用程序

XSched 旨在对应用程序保持透明。通过设置几个[环境变量](protocol/README.md)，您就可以使用XSched调度您的应用程序。请参阅我们的示例([Linux](examples/Linux/1_transparent_sched/README.md)，[Windows](examples/Windows/1_transparent_sched/README.md))以了解透明调度。

#### 链接XSched进行自定义调度

您也可以显式链接XSched并在应用程序中使用XQueue API和Hint API，以获得更高的灵活性。
更多详情请参阅我们的示例：

- 提供提示([Linux](examples/Linux/2_give_hints/README.md)，[Windows](examples/Windows/2_give_hints/README.md))
- 进程内调度([Linux](examples/Linux/3_intra_process_sched/README.md)，[Windows](examples/Windows/3_intra_process_sched/README.md))
- 手动调度([Linux](examples/Linux/4_manual_sched/README.md)，[Windows](examples/Windows/4_manual_sched/cuda/README.md))
- 非透明调度([Linux](examples/Linux/8_nontransparent_sched/README.md)，[Windows](examples/Windows/8_nontransparent_sched/cuda/README.md))

#### 更多示例

查看我们的[示例列表](examples/README.md)了解更多高级用例。


## 架构与工作流程

<img src="/docs/img/xsched-framework.png" alt="XSched framework" width="600" />

XSched由四个关键组件组成：XPU中介层(XShim)、XPU任务抢占模块(XPreempt)、XPU硬件适配层 (XAL)和XScheduler。XShim、XPreempt和XAL是预加载到每个XPU应用程序进程中的三个动态链接库，而XScheduler则作为中心系统服务守护进程运行。

- **XShim:** 代码中命名为`shim`，负责拦截XPU驱动API调用并将命令重定向到XQueue①，允许应用程序无需修改即可在XSched上运行（实现透明性）。
- **[XPreempt](preempt):** 代码中命名为`preempt`，基于多级硬件模型实现XQueue接口②。包含一个[agent](preempt/src/sched/agent.cpp)，用于监视XQueue的状态（例如，就绪或空闲），并生成调度事件以通过IPC通知XScheduler③。还负责应用从XScheduler接收到的调度操作（例如，挂起或恢复XQueue）⑤。
- **XAL:** 代码中命名为`hal`，通过调用XPU驱动程序API来实现多级硬件模型接口。
- **[XScheduler](service/server):** 代码中命名为`xserver`，协调来自不同进程的所有XQueue，通过agent报告的事件③监控全局XQueue状态，并在状态发生变化时调用调度策略做出决策。决策通过向agent发送调度操作来强制执行④。该策略是模块化且可定制的，以适应各种工作负载。
- **[XCLI](service/cli):** 一个命令行工具，可以监控XQueue状态、更改策略或提供调度提示（例如，优先级）⑥。


## 开发计划

我们将继续支持更多操作系统和平台上的XSched，并提高XSched的性能。敬请期待！

- [x] 集成到LLM服务系统中（例如 llama.cpp, vLLM）
- [x] 支持Windows
- [ ] 支持MacOS
- [ ] 安装为系统守护进程（Daemon）


## 贡献

XSched设计初衷是可扩展和灵活的。

我们欢迎以下贡献：

- 支持更多平台，或在现有平台上支持更高的抢占级别。请参阅[指南](platforms/example/README.md)
- 实现新的调度策略。请参阅[指南](sched/README.md)
- 将XSched集成到AI驱动的应用程序中。
- 报告或修复问题。


## 引用

如果您在研究中使用了XSched，请引用我们的[论文](docs/xsched-osdi25.pdf)：
```bibtex    
@inproceedings{Shen2025xsched,
  title = {{XSched}: Preemptive Scheduling for Diverse {XPU}s},
  author = {Weihang Shen and Mingcong Han and Jialong Liu and Rong Chen and Haibo Chen},
  booktitle = {19th USENIX Symposium on Operating Systems Design and Implementation (OSDI 25)},
  year = {2025},
  isbn = {978-1-939133-47-2},
  address = {Boston, MA},
  pages = {671--692},
  url = {https://www.usenix.org/conference/osdi25/presentation/shen-weihang},
  publisher = {USENIX Association},
  month = jul
}
```

XSched的artifact发布在[Github](https://github.com/XpuOS/xsched-artifacts)和[Zenodo](https://doi.org/10.5281/zenodo.15327992)上。

## 联系我们

- 对于技术问题和功能请求，请通过GitHub [Issues](https://github.com/XpuOS/xsched/issues)提交
- 对于合作与伙伴关系，请联系[rongchen@sjtu.edu.cn](mailto:rongchen@sjtu.edu.cn)
