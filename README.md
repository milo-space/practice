# practice


RTX 5070 Ti / Windows 11
Nerfstudio main + gsplat 導入手順

前提:
- Python 3.10、Git、CUDA Toolkit 12.8、Visual Studio 2022「C++によるデスクトップ開発」をインストール済み
- 「x64 Native Tools Command Prompt for VS 2022」で以下を実行
- データセットは transforms.json 作成済み

1. 仮想環境とPyTorch

cd /d U:\NerfstudioProject
mkdir Nerfstudio
cd /d U:\NerfstudioProject\Nerfstudio
py -3.10 -m venv .venv
call .venv\Scripts\activate.bat
python -m pip install --upgrade pip setuptools wheel ninja
python -m pip install torch==2.8.0 torchvision==0.23.0 --index-url https://download.pytorch.org/whl/cu128

2. Nerfstudio

git clone https://github.com/nerfstudio-project/nerfstudio.git nerfstudio-src
cd /d U:\NerfstudioProject\Nerfstudio\nerfstudio-src
python -m pip install -e .
python -m pip install Pillow==11.3.0
python -m pip uninstall -y gsplat

3. gsplatをソースからビルド

cd /d U:\NerfstudioProject\Nerfstudio
git clone --recursive --branch v1.4.0 https://github.com/nerfstudio-project/gsplat.git gsplat-src
set "CUDA_HOME=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.8"
set "PATH=%CUDA_HOME%\bin;%CUDA_HOME%\lib\x64;%PATH%"
set "DISTUTILS_USE_SDK=1"
set "MAX_JOBS=8"
set "CMAKE_BUILD_PARALLEL_LEVEL=8"
set "TORCH_CUDA_ARCH_LIST=12.0"
cd /d U:\NerfstudioProject\Nerfstudio\gsplat-src
python -m pip install -v --no-build-isolation --no-cache-dir .

4. 確認と学習

python -c "import torch, gsplat, gsplat.csrc, PIL; print('Torch:', torch.__version__); print('CUDA:', torch.version.cuda); print('GPU:', torch.cuda.get_device_name(0)); print('gsplat:', gsplat.__version__); print('Pillow:', PIL.__version__)"
ns-train splatfacto --data "U:\NerfstudioProject\Nerfstudio\datasets\Test_Rock"
ns-train splatfacto-mcmc --data "U:\NerfstudioProject\Nerfstudio\datasets\Test_Rock"
