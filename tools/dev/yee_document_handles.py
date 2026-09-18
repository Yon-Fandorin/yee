"""Connection-local presentation aliases; never a replacement for native consent."""
import secrets


class DocumentHandles:
    def __init__(self):
        self.prefix='~'+secrets.token_hex(6)+'.'
        self.counter=0
        self.document=None
        self.handle=None

    def present(self, response):
        result=dict(response)
        doc=result.get('document')
        if not isinstance(doc,str):return result
        if not doc:
            self.document=self.handle=None
            return result
        if doc!=self.document:
            self.counter+=1
            self.document=doc
            self.handle=self.prefix+str(self.counter)
        result['document']=self.handle
        return result

    def expand(self, document):
        if isinstance(document,str) and document.startswith('~'):
            if document!=self.handle or self.document is None:
                raise ValueError('unknown or expired connection-local document handle; observe again')
            return self.document
        return document
