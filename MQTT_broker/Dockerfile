## MQTT broker

FROM ubuntu:bionic
LABEL maintainer Christoph.c.b.schmidt@stud.h-da.de

# Install MQTT libraries
RUN apt-get update && apt-get install -y libmosquitto-dev

# Install Mosquitto broker
RUN apt-get update && apt-get install -y mosquitto

# Start the Mosquitto broker when the container is run
CMD /usr/sbin/mosquitto -c /etc/mosquitto/mosquitto.conf
