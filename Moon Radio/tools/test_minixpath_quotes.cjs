// Replay MiniXPath's C++ find() state machine in Node for XML quote regressions.
// Its switch uses shared C++/JS syntax; adapt only constants and member access.
// Complements firmware compilation and the real-device browse test.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const test = require('node:test');
const dir = path.join(__dirname, '../lib/SoapESP32/src');
const cpp = fs.readFileSync(path.join(dir, 'MiniXPath.cpp'), 'utf8');
const header = fs.readFileSync(path.join(dir, 'MiniXPath.h'), 'utf8');
let body = cpp.split('bool MiniXPath::find(char charToParse, bool subTree)')[1].split('#ifdef MINIXPATH_DEBUG')[0];
body = body.slice(body.indexOf('{') + 1).replace(/\/\/[^\n]*/g, '')
  .replace(/strlen\(([^()]*)\)/g, '($1).length');
for (const member of ['state','matchLevel','pathSize','treeFlag','subLevel','sub',
  'tagLevel','position','matchCount','path','attributeQuote']) {
  body = body.replace(new RegExp('\\b' + member + '\\b', 'g'), 'this.' + member);
}
body = body.replace(/elementPathMatch\(\)/g, 'this.elementPathMatch()');
for (const match of header.matchAll(/^#define\s+(XML_PARSER_\w+)\s+(\d+)/gm)) {
  body = body.replace(new RegExp('\\b' + match[1] + '\\b', 'g'), match[2]);
}
body += 'return this.matchLevel === this.pathSize;';
class Parser {
  constructor(tags) {
    Object.assign(this, {state:0, matchLevel:0, pathSize:tags.length, treeFlag:false,
      subLevel:0, sub:true, tagLevel:0, position:0, matchCount:0, path:tags, attributeQuote:0});
  }
  elementPathMatch() {
    return this.matchLevel < this.pathSize && this.matchCount === this.position &&
      (this.sub || this.tagLevel - this.subLevel < this.pathSize) &&
      this.matchCount === this.path[this.matchLevel].length;
  }
}
Parser.prototype.find = new Function('charToParse', 'subTree', body);
function values(xml, tag) {
  const p = new Parser(['u:BrowseResponse', tag]); const out = []; let value = '';
  for (const c of xml) {
    if (p.find(c, false)) {
      if (p.state === 11 && p.position > 0) value += c;
      else if (p.state === 13 && p.position === 0) out.push(value.trim());
    } else if (p.tagLevel - p.subLevel === p.matchLevel && p.state === 2 && p.position === 0) value = '';
  }
  assert.equal(p.tagLevel, 0, 'XML nesting must remain balanced');
  return out;
}
for (const [name, attributes] of [
  ['ordinary identifier', 'id="/Music/Track.mp3" parentID="/Music"'],
  ['apostrophe inside double quotes', 'id="/Music/I\'ll Be Missing You.mp3" parentID="/Music"'],
  ['double quote inside single quotes', `id='/Music/Say "Hello".mp3' parentID='/Music'`],
  ['mixed delimiters across attributes', `id="/Music/I'll.mp3" parentID='/Music/"Mix"'`]
]) {
  test(name, () => {
    const xml = '<s:Envelope><s:Body><u:BrowseResponse><Result><DIDL-Lite>' +
      `<item ${attributes}><dc:title>Track</dc:title><res size="42"/>` +
      '</item></DIDL-Lite></Result><NumberReturned>1</NumberReturned>' +
      '<TotalMatches>1</TotalMatches></u:BrowseResponse></s:Body></s:Envelope>';
    assert.deepEqual(values(xml, 'NumberReturned'), ['1']);
    assert.deepEqual(values(xml, 'TotalMatches'), ['1']);
  });
}
