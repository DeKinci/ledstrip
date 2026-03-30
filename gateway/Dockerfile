FROM golang:1.23-alpine AS build
RUN apk add --no-cache gcc musl-dev bash npm
WORKDIR /src

# Go deps
COPY go.mod go.sum ./
RUN go mod download

# Widget build
COPY ../widgets/ /src/widgets/
COPY web/ /src/web/
RUN cd web && npm install --prefix /src/widgets esbuild && bash build.sh

# Copy source + built static assets
COPY . .
RUN CGO_ENABLED=1 go build -o /gateway ./cmd/gateway/

FROM alpine:3.20
RUN apk add --no-cache ca-certificates
COPY --from=build /gateway /gateway
EXPOSE 8080
ENTRYPOINT ["/gateway"]
