FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    libcurl4-openssl-dev \
    ca-certificates \
    zlib1g-dev \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY back-end/ ./back-end/
COPY front-end/ ./front-end/

RUN g++ -std=c++17 -O2 -Wall -Wextra -pedantic \
    back-end/server.cpp back-end/namegen.cpp back-end/history_store_gist.cpp \
    -lcurl -lz -o /app/server

ENV PORT=8080
EXPOSE 8080

CMD ["/app/server"]

