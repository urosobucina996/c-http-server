# ---------- config ----------
IMAGE_NAME := c-http-server
CONTAINER_NAME := c-http-server
PORT := 8080

# ---------- phony targets ----------
.PHONY: build run stop restart clean logs exec

# ---------- build docker image ----------
build:
	docker build -t $(IMAGE_NAME) .

# ---------- run container ----------
run:
	docker run --rm \
		--name $(CONTAINER_NAME) \
		-p $(PORT):8080 \
		$(IMAGE_NAME)

# ---------- stop container ----------
stop:
	docker stop $(CONTAINER_NAME) || true

# ---------- restart container ----------
restart: stop run

# ---------- remove image ----------
clean:
	docker rmi $(IMAGE_NAME) || true

# ---------- enter container ----------
exec:
	docker exec -it $(CONTAINER_NAME) /bin/sh

# ---------- view logs ----------
logs:
	docker logs -f $(CONTAINER_NAME)
