FROM ubuntu:20.04
RUN apt update && apt install -y git build-essential autoconf automake dos2unix

WORKDIR /app
ADD . ./

RUN dos2unix configure && chmod +x configure


RUN ./configure --prefix='/usr' --enable-shared --disable-static  \
    && make \
    && make install


# use this to release
# docker build . -t l-smash ^C
# docker tag l-smash l-smash:2.14.6-beta