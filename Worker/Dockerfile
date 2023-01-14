##Worker

## Build stage
FROM thrift_install:1 AS build
LABEL maintainer Christoph.c.b.schmidt@stud.h-da.de
LABEL stage build

RUN mkdir -p /app

COPY ./Worker /app
COPY ./gen-cpp /app/gen-cpp/ 

WORKDIR /app
RUN make WITH_MQTT=1




## Run stage
FROM thrift_install:1 AS run
LABEL maintainer Christoph.c.b.schmidt@stud.h-da.de 
LABEL stage run

RUN mkdir -p /app
COPY --from=build /app /app/
WORKDIR /app
CMD ./worker.o

