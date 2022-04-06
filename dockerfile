FROM ubuntu:20.04


# Add a non-root user "octave" with his home folder
RUN useradd -ms /bin/bash octave

################
# ROOT SECTION #
################

USER root
WORKDIR /

# Tools
RUN apt update && apt install -qq -y ca-certificates curl apt-transport-https lsb-release gnupg xxd
RUN curl -sL https://packages.microsoft.com/keys/microsoft.asc | gpg --dearmor |tee /etc/apt/trusted.gpg.d/microsoft.asc.gpg > /dev/null
RUN AZ_REPO=$(lsb_release -cs) && echo "deb [arch=amd64] https://packages.microsoft.com/repos/azure-cli/ $AZ_REPO main" |tee /etc/apt/sources.list.d/azure-cli.list
RUN dpkg --add-architecture i386
RUN apt -qq update && apt-get -q upgrade -y
RUN DEBIAN_FRONTEND=noninteractive apt install -qq -y python-is-python3 cmake git ninja-build python-jinja2 dh-autoreconf wget curl jq zip azure-cli libcmocka-dev libcmocka-dev:i386 zlib1g-dev zlib1g-dev:i386 gcc-multilib libssl-dev

# Install repo tool for python3
RUN curl https://storage.googleapis.com/git-repo-downloads/repo > /usr/bin/repo
RUN chmod a+rx /usr/bin/repo

# Toolchain WP77
ENV TOOLCHAIN poky-swi-ext-glibc-x86_64-meta-toolchain-swi-armv7a-neon-swi-mdm9x28-wp-toolchain-swi-SWI9X06Y_03.00.05.00.sh
ENV AZURE_STORAGE_ACCOUNT octavebuild
RUN wget -q -O $TOOLCHAIN https://$AZURE_STORAGE_ACCOUNT.blob.core.windows.net/build/$TOOLCHAIN
RUN chmod a+x poky*.sh
RUN ./$TOOLCHAIN -y -d /opt/swi/y22-ext-wp77xx

RUN wget -q https://downloads.sierrawireless.com/tools/swicwe/swicwe_latest.deb -O swicwe_latest.deb
RUN apt-get -qq -y install ./swicwe_latest.deb

####################
# NON-ROOT SECTION #
####################

USER octave
WORKDIR /home/octave

# Setup a valid SSH key, this is required to access Github even when repo is public
RUN mkdir .ssh
RUN echo "$GITHUB_ID_RSA" > /home/octave/.ssh/id_rsa && chmod 600 /home/octave/.ssh/id_rsa
RUN echo "$GITHUB_ID_RSA_PUB" > /home/octave/.ssh/id_rsa.pub && chmod 600 /home/octave/.ssh/id_rsa.pub
RUN eval "$(ssh-agent -s)" && ssh-add /home/octave/.ssh/id_rsa
RUN ssh-keyscan -t rsa github.com >> /home/octave/.ssh/known_hosts

# Public Legato release
RUN mkdir /home/octave/legato_framework
RUN cd /home/octave/legato_framework && repo init -u git@github.com:legatoproject/manifest -m legato/releases/21.05.0/legato.xml
RUN cd /home/octave/legato_framework && repo sync

ENV LEGATO_ROOT /home/octave/legato_framework/legato
ENV WP77XX_SYSROOT /opt/swi/y22-ext-wp77xx/sysroots/armv7a-neon-poky-linux-gnueabi


# Apply patches on Legato
WORKDIR $LEGATO_ROOT

# Build
RUN make wp77xx
