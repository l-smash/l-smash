FROM ubuntu:20.04
RUN apt update && apt install -y git build-essential autoconf automake dos2unix

WORKDIR /app
ADD . ./

RUN dos2unix configure && chmod +x configure


RUN ./configure --prefix='/usr' --enable-shared --disable-static  \
    && make \
    && make install \
    && mkdir -p /build/bin \
    && mkdir -p /build/lib \
    && mkdir -p /build/include \
    &&  ./configure --prefix='/build' --enable-shared --disable-static \
    && make install

RUN rm -rf /app/*

# use this to build
#   docker build . -t l-smash

# this to release
#   docker tag l-smash l-smash:2.14.6-beta

# use this to copy artifacts:
#   docker run --rm -v "$PWD/artifacts:/artifacts" l-smash bash -c "cp -r /build/* /artifacts/" 