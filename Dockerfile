FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y g++ cmake make libssl-dev wget ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY backend/ .
RUN mkdir -p third_party/nlohmann \
 && wget -q -O third_party/httplib.h https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.18.0/httplib.h \
 && wget -q -O third_party/nlohmann/json.hpp https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j2

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y libssl3 ca-certificates && rm -rf /var/lib/apt/lists/*
COPY --from=build /app/build/stockify /usr/local/bin/stockify
EXPOSE 8080
CMD ["stockify"]
