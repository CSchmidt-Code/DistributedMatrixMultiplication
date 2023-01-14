##CONTROLLER

## Build stage
FROM thrift_install:1 AS build
LABEL maintainer Christoph.c.b.schmidt@stud.h-da.de
LABEL stage build

RUN mkdir -p /app

COPY ./Controller /app
COPY ./gen-cpp /app/gen-cpp

WORKDIR /app
RUN make




## Run stage
FROM thrift_install:1 AS run
LABEL maintainer Christoph.c.b.schmidt@stud.h-da.de 
LABEL stage run

EXPOSE 10000/udp

RUN mkdir -p /app
COPY --from=build /app /app/
WORKDIR /app
CMD ./controller.o











# sudo docker build . -t vs_p_controller:1.0.0 
# docker run vs_p_controller:1.0.0