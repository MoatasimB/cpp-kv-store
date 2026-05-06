FROM ubuntu:24.04 AS build

RUN apt-get update && \
    apt-get install -y --no-install-recommends build-essential cmake && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

RUN cmake -S . -B build && \
    cmake --build build

FROM ubuntu:24.04

RUN apt-get update && \
    apt-get install -y --no-install-recommends libstdc++6 && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /app/build/kv_store /app/kv_store

EXPOSE 6380
VOLUME ["/data"]

CMD ["/app/kv_store", "6380", "/data/snapshot.db"]
