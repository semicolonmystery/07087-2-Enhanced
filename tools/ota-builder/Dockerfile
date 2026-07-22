FROM ubuntu:22.04

# Avoid tzdata interactive prompt
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y wget unzip python3 git && \
    rm -rf /var/lib/apt/lists/*

# Download and extract Simplicity Commander for Linux
RUN wget -q -O commander.zip https://www.silabs.com/documents/login/software/SimplicityCommander-Linux.zip && \
    unzip -q commander.zip -d /opt/commander && \
    rm commander.zip

# Add Commander to PATH
ENV PATH="/opt/commander/Commander_linux_x86_64:${PATH}"

# Create working directory for the repository mount
WORKDIR /repo

# Run the create_ota.py script by default
CMD ["python3", ".github/scripts/create_ota.py"]
