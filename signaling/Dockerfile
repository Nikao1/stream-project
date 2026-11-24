# Servidor de sinalizacao do Colae em um container.
#
#   docker build -t colae-sinalizacao signaling
#   docker run -d --restart unless-stopped -p 8080:8080 colae-sinalizacao
#
# Sem dependencias alem do Python.
FROM python:3.12-slim

WORKDIR /app

COPY signaling_server.py .

ENV PORT=8080
EXPOSE 8080

# Usuario sem privilegios.
RUN useradd --create-home colae
USER colae

CMD ["python", "-u", "signaling_server.py"]
