// Configuration transactionnelle. Les mots de passe ne quittent jamais le formulaire
// sauf lors de la requete POST vers l'ESP32 ; aucun stockage local des secrets.
async function loadEspConfig(){
 const target=document.getElementById('config-status');
 try{
  const resp=await apiFetch('/api/config',{cache:'no-store'});
  if(!resp.ok)throw Error('HTTP '+resp.status);
  const cfg=await resp.json();
  document.getElementById('config-ssid').value=cfg.ssid||'';
  document.getElementById('config-mac').value=cfg.mac||'';
  document.getElementById('config-level').value=String(cfg.diagnostics||0);
  target.textContent=cfg.awaiting_confirmation?'Connexion reussie : confirmer maintenant':cfg.pending?'Confirmer physiquement avec IO0':'Parametres charges';
  return cfg;
 }catch(e){target.textContent='Lecture impossible : '+e.message;return null}
}
document.getElementById('config-form').addEventListener('submit',async event=>{
 event.preventDefault();
 const form=event.currentTarget;
 const target=document.getElementById('config-status');
 try{
  const body=new URLSearchParams(new FormData(form));
  const result=await apiFetch('/api/config/stage',{method:'POST',body,cache:'no-store'});
  const data=await result.json();
  target.textContent=data.message+(result.ok?' — maintenir IO0 2 secondes':'');
  form.querySelector('#config-password').value='';
  form.querySelector('#config-ap-pass').value='';
 }catch(e){target.textContent='Transmission impossible : '+e.message}
});
async function confirmEspConfig(){
 const target=document.getElementById('config-status');
 const state=await loadEspConfig();
 if(!state)return;
 if(!state.awaiting_confirmation){target.textContent='Aucune connexion a confirmer. Verifier IO0 et le changement de reseau.';return}
 try{
  const response=await apiFetch('/api/config/confirm',{method:'POST',body:new URLSearchParams({nonce:String(state.nonce)}),cache:'no-store'});
  target.textContent=(await response.json()).message;
 }catch(e){target.textContent='Confirmation impossible : '+e.message}
}
