FROM gcc:11 AS builder

RUN apt-get update && apt-get install -y \
	libmysql++-dev

ADD . /src

WORKDIR /src

RUN make

CMD ./botterage