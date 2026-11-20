// LNA (Chrome): the browser may request explicit permission to access a local address.
const API_KEY='bm200-api-origin-v1';
function getApiBase(){return localStorage.getItem(API_KEY)||'http://192.168.4.1'}
function setApiBase(input){const u=new URL(input);if(!['http:','https:'].includes(u.protocol)||u.username||u.password||u.pathname!=='/'&&u.pathname!=='')throw Error('Adresse de base invalide');const origin=u.origin;localStorage.setItem(API_KEY,origin)}
function apiFetch(path,options={}){
 const url=new URL(path,getApiBase()).href;
 // targetAddressSpace is supported by recent Chromium versions; other browsers ignore it.
 return window.fetch(url,{...options,targetAddressSpace:'local',credentials:'omit'});
}
