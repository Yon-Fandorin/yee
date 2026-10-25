#!/usr/bin/env node
// CSS 스타일은 idle 작업 없이 설치하고, DOM 조건·동작은 기존 실행 경로에 남겨야 한다.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import vm from 'node:vm';
import {webcrypto} from 'node:crypto';

const source=await fs.readFile(new URL('../../renderer/content_blocking/procedural_cosmetic.js',import.meta.url),'utf8');
function run(actions, enabled=true) {
  const styles=[], idle=[];
  let observations=0;
  class CSSStyleSheet {
    replaceSync(text) {
      const match=/^([^{}]+)\{([^{}]+)\}$/.exec(text);
      if(!match)throw Error('invalid fixture CSS');
      this.cssRules=[{selectorText:match[1],style:{length:1,cssText:match[2]}}];
    }
  }
  const context=vm.createContext({
    crypto:webcrypto,Uint8Array,CSSStyleSheet,
    proceduralActions:actions.map(a=>JSON.stringify(a)),
    __yeeInsertProceduralStyle(...args){styles.push(args);return enabled;},
    __yeeProceduralEnabled(){return enabled;},
    requestIdleCallback(callback){idle.push(callback);},
    setTimeout(){throw Error('idle callback가 있는 환경에서 timer를 만들면 안 됨');},
    document:{},
    MutationObserver:class {observe(){++observations;}disconnect(){}},
    addEventListener(){},
  });
  vm.runInContext(source,context);
  return {styles,idle,observations};
}
const css=arg=>({type:'css-selector',arg});
const style={type:'style',arg:'visibility:hidden!important'};
{
  const result=run([{selector:[css('ins.adsbygoogle')],action:style},
    {selector:[css('[id^="img_"]:has(iframe)')],action:style}]);
  assert.equal(result.styles.length,2);
  assert.deepEqual(result.styles.map(s=>s[2]),['ins.adsbygoogle','[id^="img_"]:has(iframe)']);
  assert.equal(result.idle.length,0,'CSS만으로 일치하는 규칙은 idle 처리를 기다리지 않음');
  assert.equal(result.observations,0,'순수 CSS 스타일은 DOM observer가 필요하지 않음');
}
{
  const result=run([{selector:[css('.ad'),{type:'has-text',arg:'Advertisement'}],action:style}]);
  assert.equal(result.styles.length,1);
  assert.equal(result.styles[0].length,2,'조건부 스타일은 임의 marker를 사용');
  assert.equal(result.idle.length,1);
  assert.equal(result.observations,1);
}
{
  const result=run([{selector:[css('div{}body')],action:style}]);
  assert.equal(result.styles.length,0,'잘못된 selector가 여러 CSS 규칙을 주입하면 안 됨');
  assert.equal(result.idle.length,0);
}
{
  const result=run([{selector:[css('.ad')],action:style}],false);
  assert.equal(result.observations,0,'보호 끔 상태에서는 실행 규칙을 등록하지 않음');
  assert.equal(result.idle.length,0);
}
{
  const result=run([{selector:[css('.ad')],action:{type:'remove'}}]);
  assert.equal(result.styles.length,0);
  assert.equal(result.idle.length,1,'삭제 동작은 CSS 스타일로 바꾸지 않음');
}
console.log('PASS: 즉시 CSS 설치, 조건부 실행 유지, 잘못된 CSS 거부, 보호 끔과 삭제 동작 보존');
