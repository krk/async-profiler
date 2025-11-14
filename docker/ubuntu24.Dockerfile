FROM public.ecr.aws/docker/library/ubuntu:24.04

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends make gcc g++ util-linux patchelf gcovr bash tar curl ca-certificates \
    && rm -rf /var/cache/apt /var/lib/apt/lists/*
