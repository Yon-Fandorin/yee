"""Observed same-origin link plans. Page labels never grant permission."""
import json
import re
from urllib.parse import urlsplit

MAX_LINKS = 4
_STRING = r'"(?:[^"\\]|\\.)*"'
_NODE = re.compile(r'^([+~-])@([^\s]+)(?: ([a-z]+) (' + _STRING + r')(.*))?$')
_HREF = re.compile(r'^ href=(' + _STRING + r')((?: [a-z_]+)*)$')


def same_active_inventory(response, tab):
    tabs = response.get('tabs')
    return (isinstance(tab, str) and bool(tab) and response.get('snapshot') is None and
            isinstance(tabs, list) and
            all(isinstance(item, dict) and type(item.get('active')) is bool for item in tabs) and
            response.get('ok') is True and response.get('execution_settled') is True and
            response.get('receipt_persisted') is True and not response.get('partial_effect_possible') and
            response.get('tab') == tab and
            [item.get('tab') for item in tabs if item['active']] == [tab])


def origin(url):
    if (not isinstance(url, str) or not url or len(url.encode('utf-8')) > 2048 or
            any(ord(c) <= 32 or ord(c) == 127 for c in url)):
        raise ValueError('link visit requires an exact, unclipped HTTP(S) URL')
    parsed = urlsplit(url)
    if parsed.scheme not in ('http', 'https') or not parsed.hostname or parsed.username is not None or parsed.password is not None:
        raise ValueError('link visit requires credential-free HTTP(S) URLs')
    port = parsed.port
    return parsed.scheme, parsed.hostname.lower(), port if port is not None else (443 if parsed.scheme == 'https' else 80)


class ObservedLinks:
    def __init__(self):
        self.key = None
        self.links = {}

    def remember(self, response):
        snapshot = response.get('snapshot')
        if self.key is not None and same_active_inventory(response, self.key[1]):
            # Inventory URLs are cached consent metadata, not a new page
            # observation. The visit still verifies the source/hrefs natively.
            return
        document, tab, url = (response.get(k) for k in ('document', 'tab', 'url'))
        if (response.get('ok') is not True or response.get('truncated') is not False or
                not isinstance(snapshot, str) or not isinstance(document, str) or not document or
                not isinstance(tab, str) or not tab or response.get('url_truncated') or
                response.get('url_credentials_redacted')):
            self.key = None;self.links = {};return
        try: origin(url)
        except ValueError:
            self.key = None;self.links = {};return
        lines = snapshot.splitlines()
        if not lines or not re.match(r'^page @' + re.escape(document) + r' rev=[0-9]+ ', lines[0]):
            self.key = None;self.links = {};return
        key = document, tab, url
        delta = lines[0].endswith(' delta')
        if delta and key != self.key:
            self.key = None;self.links = {};return
        if not delta:self.links = {}
        self.key = key
        for line in lines[1:]:
            match = _NODE.fullmatch(line)
            if not match:continue
            ref = match[2]
            self.links.pop(ref, None)
            if match[1] == '-' or match[3] != 'link':continue
            href = _HREF.fullmatch(match[5])
            if not href or set(href[2].split()) - {'focused'}:continue
            try:
                url = json.loads(href[1])
                if origin(url) == origin(key[2]):self.links[ref] = url
            except ValueError:continue

    def plan(self, document, refs):
        if self.key is None or self.key[0] != document:
            raise ValueError('visit-links requires the latest observed document and exact current URL')
        if not isinstance(refs, list) or not 1 <= len(refs) <= MAX_LINKS:
            raise ValueError('visit-links requires 1..4 distinct observed link refs')
        native = []
        for ref in refs:
            if not isinstance(ref, str) or not ref or len(ref) > 128:
                raise ValueError('visit-links refs must be nonempty strings')
            value = ref.removeprefix('@')
            value = document + '_' + value if re.fullmatch(r'[1-9][0-9]*', value) else value
            if not re.fullmatch(re.escape(document) + r'_[1-9][0-9]*', value) or value not in self.links:
                raise ValueError('visit-links requires visible enabled, unredacted same-origin link refs')
            native.append(value)
        urls = [self.links[ref] for ref in native]
        if len(set(native)) != len(native) or len(set(urls)) != len(urls) or self.key[2] in urls:
            raise ValueError('visit-links requires distinct destinations other than the current URL')
        return self.key, native, urls


def settled_page(response, tab, url):
    return (response.get('ok') is True and response.get('execution_settled') is True and
            response.get('receipt_persisted') is True and not response.get('partial_effect_possible') and
            response.get('truncated') is False and isinstance(response.get('snapshot'), str) and
            isinstance(response.get('document'), str) and bool(response['document']) and
            response.get('tab') == tab and response.get('url') == url and
            not response.get('url_truncated') and not response.get('url_credentials_redacted'))
