# syntax = docker/dockerfile:experimental
FROM trailofbits/polytracker-llvm:851cca46f066c7cbbc3cd0c876bb5e602c99afca

MAINTAINER Evan Sultanik <evan.sultanik@trailofbits.com>
MAINTAINER Carson Harmon <carson.harmon@trailofbits.com>

### ただの時短
RUN sed -i.bak -e "s%http://archive.ubuntu.com/ubuntu/%http://ftp.iij.ad.jp/pub/linux/ubuntu/archive/%g" /etc/apt/sources.list

RUN DEBIAN_FRONTEND=noninteractive apt-get -y update
RUN DEBIAN_FRONTEND=noninteractive apt-get install -y \
      ninja-build                                     \
      python3-pip                                     \
      python3.8-dev                                   \
      libgraphviz-dev                                 \
      graphviz										  \
      libsqlite3-dev                                  \
      vim                                             \
      gdb                                             \
      sqlite3

RUN update-alternatives --install /usr/bin/python3 python3 /usr/bin/python3.8 10
RUN python3 -m pip install pip && python3 -m pip install pytest

### ただの時短
COPY requirements.txt /polytracker/
WORKDIR /polytracker
RUN pip3 install -r requirements.txt

COPY . /polytracker

RUN mkdir /polytracker/build
WORKDIR /polytracker/build
RUN cmake -GNinja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_VERBOSE_MAKEFILE=TRUE -DCXX_LIB_PATH=/cxx_libs .. 
RUN ninja install

WORKDIR /polytracker
# 自身の古いソースコードがキャッシュされるのでキャッシュはNG？
# というよりこのコードが動かなくてサイレントにビルドが失敗する
# RUN --mount=type=cache,target=/root/.cache/pip pip3 install .
# RUN --mount=type=cache,target=./whl pip3 wheel --no-cache --wheel-dir=./whl .
RUN pip3 install .

# Setting up build enviornment for targets 
ENV POLYTRACKER_CAN_RUN_NATIVELY=1
ENV CC=/polytracker/build/bin/polybuild_script
ENV CXX=/polytracker/build/bin/polybuild_script++
ENV PATH=/polytracker/build/bin:$PATH
