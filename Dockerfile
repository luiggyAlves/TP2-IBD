FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . /app

# BLOCK_SIZE pode ser sobrescrito na construcao da imagem:
#   docker build --build-arg BLOCK_SIZE=8192 -t tp2 .
ARG BLOCK_SIZE=4096
RUN make build BLOCK_SIZE=$BLOCK_SIZE

# /data e usado para o CSV de entrada e para a persistencia dos arquivos
# de dados e de indice. Nao copie o CSV para dentro da imagem.
VOLUME ["/data"]

CMD ["bash", "-lc", "echo 'use: docker run --rm -v $(pwd)/data:/data tp2 ./bin/<programa> <args>'; ls -l bin/"]
