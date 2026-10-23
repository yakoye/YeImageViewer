// 只解析用户参考包里的静态数据字面量；绝不执行参考脚本/函数。
import fs from 'node:fs';
const file='docs/color-picker/baseline-v2.2/reference/utools-color-helper-5.0.1/assets/index-CnOXSPuO.js';
const text=fs.readFileSync(file,'utf8');
function literal(start){let p=start;const ws=()=>{while(/\s/.test(text[p]??'')&&p<text.length)p++;};
 function value(){ws();const c=text[p++];if(c==='['){const a=[];ws();if(text[p]===']'){p++;return a;}do{a.push(value());ws();if(text[p]===']'){p++;return a;}if(text[p++]!==',')throw Error('array');}while(p<text.length);}
 if(c==='{'){const o={};ws();if(text[p]==='}'){p++;return o;}do{ws();let key;if(/["'`]/.test(text[p]))key=value();else{key=/^[a-zA-Z_$][\w$]*/.exec(text.slice(p))?.[0];if(!key)throw Error('key');p+=key.length;}ws();if(text[p++]!==':')throw Error('colon');o[key]=value();ws();if(text[p]==='}'){p++;return o;}if(text[p++]!==',')throw Error('object');}while(p<text.length);}
 if(c==='"'||c==="'"||c==='`'){let out='';while(p<text.length){const ch=text[p++];if(ch===c)return out;if(c==='`'&&ch==='$'&&text[p]==='{')throw Error('template expression');if(ch==='\\'){const e=text[p++];if(e==='u'){out+=String.fromCharCode(parseInt(text.slice(p,p+4),16));p+=4;}else out+=({n:'\n',r:'\r',t:'\t',b:'\b',f:'\f'}[e]??e);}else out+=ch;}throw Error('string');}
 const token=/^(?:-?\d+(?:\.\d+)?|true|false|null)/.exec(text.slice(p-1))?.[0];if(!token)throw Error('not a literal');p+=token.length-1;return JSON.parse(token);}
 return {value:value(),end:p};}
let i=0;const data=[];for(const m of text.matchAll(/Ze\(this,"data",/g)){try{const parsed=literal(m.index+m[0].length);data.push(parsed.value);console.log(i++,m.index,JSON.stringify(parsed.value).slice(0,180));}catch{}}
if(process.argv.includes('--export'))fs.writeFileSync('artifacts/reference-static-colors.json',JSON.stringify(data));
