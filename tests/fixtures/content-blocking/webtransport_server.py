"""실앱 테스트용 로컬 HTTP/3 서버. aioquic와 cryptography가 필요하다."""
import asyncio
import base64
import datetime
import hashlib
import json
from pathlib import Path
import sys

from aioquic.asyncio import QuicConnectionProtocol, serve
from aioquic.h3.connection import H3_ALPN, H3Connection
from aioquic.h3.events import DataReceived, HeadersReceived
from aioquic.quic.configuration import QuicConfiguration
from aioquic.quic.events import ProtocolNegotiated
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID


class Protocol(QuicConnectionProtocol):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.http = None
        self.closing_data = {}
        self.closed_streams = set()

    def quic_event_received(self, event):
        if isinstance(event, ProtocolNegotiated):
            self.http = H3Connection(self._quic, enable_webtransport=True)
        if self.http is None:
            return
        for received in self.http.handle_event(event):
            if isinstance(received, HeadersReceived):
                headers = dict(received.headers)
                print(json.dumps({"request": headers.get(b":path", b"").decode()}), flush=True)
                self.http.send_headers(received.stream_id, [
                    (b":status", b"200"),
                    (b"sec-webtransport-http3-draft", b"draft02"),
                ])
                self.transmit()
            elif isinstance(received, DataReceived) and received.stream_id not in self.closed_streams:
                # 이 fixture는 연결 확인 뒤 close capsule만 보낸다. 응답 FIN도
                # 보내 다음 프로필·worker의 연결에 닫히지 않은 세션을 남기지 않는다.
                data = self.closing_data.setdefault(received.stream_id, bytearray())
                data.extend(received.data)
                if received.stream_ended:
                    self.closed_streams.add(received.stream_id)
                    self.http.send_data(received.stream_id, bytes(data), end_stream=True)
                    self.closing_data.pop(received.stream_id)
                    self.transmit()


async def main():
    directory = Path(sys.argv[1])
    key = ec.generate_private_key(ec.SECP256R1())
    now = datetime.datetime.now(datetime.timezone.utc)
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "yee-transport.test")])
    certificate = (x509.CertificateBuilder().subject_name(name).issuer_name(name)
                   .public_key(key.public_key()).serial_number(x509.random_serial_number())
                   .not_valid_before(now - datetime.timedelta(hours=1))
                   .not_valid_after(now + datetime.timedelta(days=5))
                   .add_extension(x509.SubjectAlternativeName([
                       x509.DNSName("yee-transport.test"), x509.DNSName("yee-socket.test")]), False)
                   .sign(key, hashes.SHA256()))
    cert_path, key_path = directory / "certificate.pem", directory / "key.pem"
    cert_path.write_bytes(certificate.public_bytes(serialization.Encoding.PEM))
    key_path.write_bytes(key.private_bytes(serialization.Encoding.PEM,
                                         serialization.PrivateFormat.PKCS8,
                                         serialization.NoEncryption()))
    configuration = QuicConfiguration(is_client=False, alpn_protocols=H3_ALPN,
                                      max_datagram_frame_size=65536)
    configuration.load_cert_chain(str(cert_path), str(key_path))
    server = await serve("127.0.0.1", 0, configuration=configuration, create_protocol=Protocol)
    print(json.dumps({"port": server._transport.get_extra_info("sockname")[1],
                      "certificateHash": list(certificate.fingerprint(hashes.SHA256())),
                      "spkiHash": base64.b64encode(hashlib.sha256(
                          key.public_key().public_bytes(serialization.Encoding.DER,
                          serialization.PublicFormat.SubjectPublicKeyInfo)).digest()).decode()}), flush=True)
    try:
        await asyncio.to_thread(sys.stdin.readline)
    finally:
        server.close()
        await asyncio.sleep(0.1)


if __name__ == "__main__":
    if sys.platform == "win32":
        # Python 3.11 Proactor의 UDP 수신 루프는 연결 종료 후 ICMP 오류에서
        # 멈출 수 있다. 이 시험 서버는 UDP만 사용하므로 Selector로 실행한다.
        asyncio.set_event_loop_policy(asyncio.WindowsSelectorEventLoopPolicy())
    asyncio.run(main())
