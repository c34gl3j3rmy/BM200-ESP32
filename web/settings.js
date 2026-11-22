// Navigation preserves browser/Android Back behaviour.
const settingsText=(id,value)=>document.getElementById(id).textContent=value;
function showView(name,addHistory=true){
 const next=name==='settings'?'settings':'dashboard';
 document.getElementById('view-dashboard').hidden=next!=='dashboard';
 document.getElementById('view-settings').hidden=next!=='settings';
 document.getElementById('tab-dashboard').classList.toggle('active',next==='dashboard');
 document.getElementById('tab-settings').classList.toggle('active',next==='settings');
 if(addHistory)history.pushState({view:next},'',next==='settings'?'#settings':'#dashboard');
 if(next==='settings')loadWifiSettings();
}
window.addEventListener('popstate',()=>showView(location.hash==='#settings'?'settings':'dashboard',false));
showView(location.hash==='#settings'?'settings':'dashboard',false);
async function loadWifiSettings(){
 settingsText('settings-result','Lecture des paramètres…');
 try{
  const r=await apiFetch('/api/settings/wifi',{cache:'no-store'});
  if(!r.ok)throw Error('HTTP '+r.status);
  const data=await r.json();
  document.getElementById('home-ssid').value=data.home_ssid||'';
  document.getElementById('ap-ssid').value=data.ap_ssid||'';
  settingsText('settings-auth',data.unlocked?'Modifications autorisées pour encore '+data.unlock_seconds+' secondes.':'Verrouillé : maintenir BOOT 5 à 29 secondes puis relâcher.');
  settingsText('settings-result',data.ap_protected?'Wi-Fi direct protégé (mot de passe masqué).':'Wi-Fi direct ouvert (sans mot de passe).');
 }catch(e){settingsText('settings-result','Impossible de lire les réglages : '+e.message)}
}
async function submitWifi(kind){
 const prefix=kind==='home'?'home':'ap';
 const ssid=document.getElementById(prefix+'-ssid').value.trim();
 const pass=document.getElementById(prefix+'-password').value;
 const remove=kind==='ap'&&document.getElementById('ap-open').checked;
 if(!ssid || new TextEncoder().encode(ssid).length>32){settingsText('settings-result','SSID invalide : 1 à 32 octets.');return}
 if(kind==='ap' && !remove && pass.length && (pass.length<8||pass.length>63)){settingsText('settings-result','Le mot de passe WPA2 doit contenir 8 à 63 caractères.');return}
 if(remove && !confirm('Rendre le Wi-Fi direct ouvert, sans chiffrement ?'))return;
 const body=new URLSearchParams({kind,ssid});
 if(remove)body.set('password','');else if(pass)body.set('password',pass);
 settingsText('settings-result','Enregistrement en cours…');
 try{
  const r=await apiFetch('/api/settings/wifi',{method:'POST',body});
  if(!r.ok)throw Error((await r.text())||'HTTP '+r.status);
  document.getElementById(prefix+'-password').value='';
  settingsText('settings-result','Enregistré sur l’ESP32. La liaison Wi-Fi peut être interrompue : reconnecter le téléphone puis actualiser.');
 }catch(e){settingsText('settings-result','Échec de l’enregistrement : '+e.message)}
}
document.getElementById('form-home').addEventListener('submit',e=>{e.preventDefault();submitWifi('home')});
document.getElementById('form-ap').addEventListener('submit',e=>{e.preventDefault();submitWifi('ap')});
document.getElementById('ap-open').addEventListener('change',e=>{
 const field=document.getElementById('ap-password');field.disabled=e.target.checked;if(e.target.checked)field.value='';
});
