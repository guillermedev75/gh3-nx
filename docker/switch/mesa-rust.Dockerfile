# Camada sobre o Docker.rust do mesa-switch. O wrapper do rustc compila para aarch64-unknown-linux-gnu,
# mas a imagem só traz o std do host: num host arm64 ele já vem, num amd64 (o caso aqui) falta
# ("can't find crate for `std`" em libcompiler.rlib).
ARG BASE
FROM ${BASE}
RUN rustup target add aarch64-unknown-linux-gnu
