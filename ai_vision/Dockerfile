FROM nvidia/cuda:12.9.2-devel-ubuntu24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
	python3-pip \
	python3-dev \
	python3-venv

RUN python3 -m venv /opt/venv

ENV PATH="/opt/venv/bin:$PATH"

RUN python3 -m pip install --no-cache-dir --upgrade pip

RUN pip3 install --no-cache-dir torch torchvision --index-url https://download.pytorch.org/whl/cu129

RUN apt-get update && apt-get install -y \
	libxcb-cursor0 \
	libgl1 \
	libglib2.0-0 \
	libcurl4-openssl-dev

RUN pip3 install --no-cache-dir ultralytics ultralytics-thop fiftyone kaggle umap-learn rich 'graphql-core<3.3.0'



WORKDIR /workspace

CMD ["bash"]
