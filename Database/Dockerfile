## Database

## Build stage
FROM gcc:12.2.0 AS build
LABEL maintainer Christoph.c.b.schmidt@stud.h-da.de
LABEL stage build

COPY . /usr/src/database
WORKDIR /usr/src/database
RUN make


## Run stage
FROM gcc:12.2.0 AS run
LABEL maintainer Christoph.c.b.schmidt@stud.h-da.de 
LABEL stage run

EXPOSE 80/tcp

RUN mkdir -p /app
COPY --from=build /usr/src/database /app/
WORKDIR /app
CMD ./database.o