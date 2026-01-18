# -------- build stage --------
FROM alpine:3.20 AS build
RUN apk add --no-cache build-base
WORKDIR /app
COPY . .
RUN cc -Wall -Wextra -O2 -s -o server src/main.c

# -------- runtime stage --------
FROM alpine:3.20
WORKDIR /app
COPY --from=build /app/server /app/server

# run as non-root
RUN adduser -D -H -s /sbin/nologin appuser
USER appuser

EXPOSE 8080
CMD ["/app/server"]
