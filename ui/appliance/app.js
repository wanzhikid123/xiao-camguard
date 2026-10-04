'use strict';
const $ = id => document.getElementById(id);
let csrf = '', sessionGeneration = 0, polling = null, previewing = false, stillUrl = '';
let busy = false, lastStatus = null;
let setupNonce = '', setupTicket = '', setupRevision = 0, setupPolling = null;
let mediaCursor = '', datesCursor = '', mediaBusy = false;
const mediaSelected = new Set();
let otaUploading = false;
let wgModeDirty = false;
let captureValues = null, captureDefaults = null;
const captureFields = [
  ['videoResolution','录像分辨率'],['photoResolution','照片分辨率'],['fps','录像目标帧率',1,30],
  ['videoQuality','录像 JPEG 品质',5,63],['photoQuality','照片 JPEG 品质',5,63],
  ['brightness','亮度',-2,2],['contrast','对比度',-2,2],['saturation','饱和度',-2,2],
  ['sharpness','锐度',-3,3],['denoise','降噪',0,8],['effect','色彩效果'],
  ['autoWhiteBalance','自动白平衡'],['whiteBalanceGain','白平衡增益'],['whiteBalance','白平衡模式'],
  ['autoExposure','自动曝光'],['exposureLevel','曝光补偿',-2,2],['exposure','手动曝光',0,1200],
  ['autoGain','自动传感器增益'],['gain','手动传感器增益',0,30],['gainCeiling','传感器增益上限'],
  ['mirror','水平镜像'],['flip','垂直翻转'],['badPixelCorrection','坏点校正'],
  ['whitePixelCorrection','亮点校正'],['gamma','伽马校正'],['lensCorrection','镜头校正'],
  ['micGain','麦克风增益'],['cpuMHz','CPU 频率'],['powerSave','空闲省电'],
  ['idleSeconds','空闲等待秒数',30,1800],['minimumFreeMB','保留 SD 空间（MB）',1,1048576]
];
const toggleFields = new Set(['autoWhiteBalance','whiteBalanceGain','autoExposure','autoGain','mirror','flip',
  'badPixelCorrection','whitePixelCorrection','gamma','lensCorrection','powerSave']);
let tlSessionCursor='',tlFileCursor='',tlSessions=[];
function updateFeatures(status){
  const tl=status.timelapse,conversion=status.conversion;
  if(tl){$('tl-start').disabled=busy||tl.active||status.events?.armed||status.events?.requested||status.recording||!status.cameraReady;
    $('tl-stop').disabled=busy||!tl.active;for(const id of ['tl-preset','tl-interval','tl-immediate'])$(id).disabled=Boolean(tl.active);
    message('tl-message',t('定时拍照：{state} · 已保存 {photos} · 跳过 {skipped} · 失败 {failures} · 下次 {seconds} 秒',{
      state:t(tl.active?'运行中':'已停止'),photos:tl.photos,skipped:tl.skipped,failures:tl.failures,seconds:Math.ceil(tl.nextInSeconds)})+(tl.error&&tl.error!=='none'?' · '+tl.error:''),tl.error&&tl.error!=='none');
    $('tl-delete').disabled=!$('tl-sessions').value||($('tl-sessions').value===tl.session&&tl.active);
  }
  if(conversion){$('transcode-enabled').checked=conversion.enabled;
    const labels={waiting_idle:'等待空闲',paused:'已暂停',disabled:'已关闭',converting:'转换中',completed:'已完成',unsupported:'原分辨率不支持',destination_exists:'同名 MP4 已存在',failed:'失败'};
    message('transcode-message',t('转换：{state} · {frames}/{total} 帧 · 完成 {completed} · 失败 {failed}',{
      state:t(labels[conversion.state]||'等待空闲'),frames:conversion.frames,total:conversion.totalFrames,completed:conversion.completed,failed:conversion.failed})
      +(conversion.source?' · '+conversion.source:'')+(conversion.error?' · '+conversion.error:''));}
}
async function featureCommand(path,value){const options={method:'POST'};
  if(value!==undefined){options.body=JSON.stringify(value);options.headers={'Content-Type':'application/json'};}
  const response=await api(path,options);let body={};try{body=await response.json();}catch(_){}
  if(!response.ok)throw new Error(body.error||String(response.status));return body;
}
$('tl-preset').addEventListener('change',()=>{if($('tl-preset').value!=='custom')$('tl-interval').value=$('tl-preset').value;});
$('tl-interval').addEventListener('input',()=>{$('tl-preset').value='custom';});
$('timelapse-form').addEventListener('submit',async event=>{event.preventDefault();if(busy)return;
  if(lastStatus?.events?.requested||lastStatus?.events?.armed){message('tl-message',t('请先停止事件监测，再开始定时拍照。'),true);return;}
  busy=true;stopPreview();try{await featureCommand('/api/timelapse/start',{intervalSeconds:Number($('tl-interval').value),immediate:$('tl-immediate').checked});await refreshTimelapseSessions();}
  catch(error){message('tl-message',t('操作未完成：{error}',{error:error.message}),true);}finally{busy=false;refreshStatus(sessionGeneration);}});
$('tl-stop').addEventListener('click',async()=>{try{await featureCommand('/api/timelapse/stop');setTimeout(()=>refreshTimelapseSessions(),500);}
  catch(error){message('tl-message',t('操作未完成：{error}',{error:error.message}),true);}refreshStatus(sessionGeneration);});
$('transcode-enabled').addEventListener('change',async()=>{const desired=$('transcode-enabled').checked;$('transcode-enabled').disabled=true;
  try{await featureCommand('/api/settings/transcode',{enabled:desired});}catch(error){$('transcode-enabled').checked=!desired;message('transcode-message',t('操作未完成：{error}',{error:error.message}),true);}
  finally{$('transcode-enabled').disabled=false;refreshStatus(sessionGeneration);}});
$('ble-show').addEventListener('click',async()=>{const generation=sessionGeneration;try{const response=await api('/api/settings/ble');if(!response.ok)throw new Error(String(response.status));
  const ble=await response.json();if(generation!==sessionGeneration)return;
  message('ble-message',t('设备 {name} · 配对 PIN：{pin}',{name:ble.name,pin:ble.pin}));}
  catch(error){if(generation===sessionGeneration)message('ble-message',t('操作未完成：{error}',{error:error.message}),true);}});
$('ble-rotate').addEventListener('click',async()=>{if(!confirm(t('更换 PIN 将断开蓝牙并清除所有配对，确定继续？')))return;
  try{await featureCommand('/api/settings/ble');message('ble-message',t('PIN 更换已请求。请稍后查看新 PIN 并重新配对。'));}
  catch(error){message('ble-message',t('操作未完成：{error}',{error:error.message}),true);}});
async function refreshTimelapseSessions(append=false){const generation=sessionGeneration;if(!csrf)return;
  try{const response=await api('/api/timelapse/sessions'+(append?'?before='+encodeURIComponent(tlSessionCursor):''));if(!response.ok)throw new Error(String(response.status));
    const page=await response.json();if(generation!==sessionGeneration)return;const selected=$('tl-sessions').value;
    if(!append){tlSessions=[];$('tl-sessions').replaceChildren();}
    for(const item of page.items){tlSessions.push(item);const option=document.createElement('option');option.value=item.name;
      option.textContent=item.name+(item.running?' · '+t('运行中'):'');$('tl-sessions').append(option);}
    if(tlSessions.some(item=>item.name===selected))$('tl-sessions').value=selected;
    tlSessionCursor=page.nextBefore;$('tl-more-sessions').hidden=!page.more;
    if(page.enumerationTruncated)message('tl-session-details',t('目录较大，本次扫描已达到上限。'),true);
    await refreshTimelapseFiles();
  }catch(error){message('tl-session-details',t('操作未完成：{error}',{error:error.message}),true);}
}
async function refreshTimelapseFiles(append=false){const name=$('tl-sessions').value,generation=sessionGeneration;
  if(!append)$('tl-files').replaceChildren();$('tl-delete').disabled=!name||Boolean(lastStatus?.timelapse?.active&&lastStatus.timelapse.session===name);
  if(!name){message('tl-session-details',t('暂无拍照文件夹'));return;}
  const selected=tlSessions.find(item=>item.name===name),meta=selected?.metadata;
  if(meta)$('tl-session-details').textContent=`${meta.width} × ${meta.height} · JPEG ${meta.quality} · ${meta.intervalSeconds} s · ${t('照片')} ${meta.photos} · ${t('跳过')} ${meta.skipped} · ${meta.error||''}`;
  try{const response=await api('/api/timelapse/files?session='+encodeURIComponent(name)+(append?'&before='+encodeURIComponent(tlFileCursor):''));
    if(!response.ok)throw new Error(String(response.status));const page=await response.json();if(generation!==sessionGeneration||name!==$('tl-sessions').value)return;
    for(const item of page.items){const row=document.createElement('div'),label=document.createElement('span'),download=document.createElement('a'),view=document.createElement('button');
      row.className='media-actions';label.textContent=item.name+' · '+size(item.bytes);download.href='/api/media/download?path='+encodeURIComponent(item.path);download.textContent=t('下载');
      view.type='button';view.textContent=t('预览');view.addEventListener('click',()=>{stopPreview();$('preview').src='/api/media/file?path='+encodeURIComponent(item.path);
        $('preview').hidden=false;$('preview-placeholder').hidden=true;$('preview').scrollIntoView({block:'center',behavior:'smooth'});});row.append(label,view,download);$('tl-files').append(row);}
    tlFileCursor=page.nextBefore;$('tl-more-files').hidden=!page.more;
    if(page.enumerationTruncated)message('tl-session-details',t('目录较大，本次扫描已达到上限。'),true);
  }catch(error){message('tl-session-details',t('操作未完成：{error}',{error:error.message}),true);}
}
$('tl-refresh').addEventListener('click',()=>refreshTimelapseSessions());$('tl-sessions').addEventListener('change',()=>refreshTimelapseFiles());
$('tl-more-sessions').addEventListener('click',()=>refreshTimelapseSessions(true));$('tl-more-files').addEventListener('click',()=>refreshTimelapseFiles(true));
$('tl-delete').addEventListener('click',async()=>{const name=$('tl-sessions').value;if(!name||!confirm(t('删除文件夹 {name} 中的全部照片？此操作无法撤销。',{name})))return;
  try{await featureCommand('/api/timelapse/delete',{session:name});await refreshTimelapseSessions();}
  catch(error){message('tl-session-details',t('操作未完成：{error}',{error:error.message}),true);}});
function stopMediaVideo(){const video=$('media-video');video.pause();video.removeAttribute('src');video.load();video.hidden=true;}
function signal(rssi) { return t(rssi >= -50 ? t('优秀') : rssi >= -60 ? t('良好') : rssi >= -70 ? t('一般') : rssi >= -80 ? t('较差') : t('很差')); }
async function refreshCaptureSettings() {
  const generation = sessionGeneration;
  try {
    const response = await api('/api/settings/capture');
    if (!response.ok) throw new Error('settings');
    const settings = await response.json(); if (generation !== sessionGeneration) return;
    captureValues = settings.values; captureDefaults = settings.defaults;
    const options = {
      videoResolution: settings.videoResolutions.map(item => [item.value, `${item.width} × ${item.height}`]),
      photoResolution: settings.photoResolutions.map(item => [item.value, `${item.width} × ${item.height}`]),
      effect: [[0,'自然'],[1,'负片'],[2,'黑白'],[3,'红色'],[4,'绿色'],[5,'蓝色'],[6,'棕褐色']],
      whiteBalance: [[0,'自动'],[1,'晴天'],[2,'阴天'],[3,'办公室'],[4,'家用灯光']],
      gainCeiling: [2,4,8,16,32,64,128].map((value,index)=>[index,value+'×']),
      micGain: [[0,'关闭（仅预览与拍照）'],[1,'0.25× (−12 dB)'],[2,'0.5× (−6 dB)'],[3,'1× (0 dB)'],
        [4,'2× (+6 dB)'],[5,'4× (+12 dB)'],[6,'8× (+18 dB)'],[7,'16× (+24 dB)']],
      cpuMHz: [[80,'80 MHz'],[160,'160 MHz'],[240,'240 MHz']],
    };
    $('capture-fields').replaceChildren();
    for (const [name,label,min,max] of captureFields) {
      const row = document.createElement('div'), caption = document.createElement('label');
      caption.htmlFor = 'setting-'+name; caption.dataset.i18n = label; caption.textContent = t(label);
      const choices = toggleFields.has(name) ? [[1,'开启'],[0,'关闭']] : options[name];
      const input = document.createElement(choices ? 'select' : 'input'); input.id = caption.htmlFor; input.dataset.setting = name;
      if (choices) for (const [value,text] of choices) { const option=document.createElement('option'); option.value=value;
        option.dataset.i18n=text; option.textContent=t(text); input.append(option); }
      else {input.type='number';input.min=min;input.max=max;input.step=1;input.required=true;}
      input.value=captureValues[name]; row.append(caption,input);$('capture-fields').append(row);
    }
    message('capture-message', settings.error !== 'none' ? t('参数存储状态：{error}',{error:settings.error}) : t('参数会保存在设备中，重启后继续使用。'));
    updateManualFields();
  } catch (_) { message('capture-message', t('暂时无法读取摄像参数。'), true); }
}
function updateManualFields() {
  if (!$('setting-autoExposure')) return;
  $('setting-exposure').disabled = $('setting-autoExposure').value === '1';
  $('setting-gain').disabled = $('setting-autoGain').value === '1';
  $('setting-whiteBalance').disabled = $('setting-autoWhiteBalance').value === '0';
}
$('capture-fields').addEventListener('change', updateManualFields);
$('capture-defaults').addEventListener('click', () => {
  if (!captureDefaults) return;
  for (const input of $('capture-fields').querySelectorAll('[data-setting]')) input.value = captureDefaults[input.dataset.setting];
  updateManualFields(); message('capture-message', t('推荐参数已填入，点击保存后生效。'));
});
$('capture-form').addEventListener('submit', async event => {
  event.preventDefault(); if (busy || lastStatus?.recording || lastStatus?.recordRequested || lastStatus?.events?.requested || lastStatus?.events?.armed) return;
  const values = {}; for (const input of $('capture-fields').querySelectorAll('[data-setting]')) values[input.dataset.setting] = Number(input.value);
  busy=true; $('capture-submit').disabled=true; stopPreview();
  try {
    const response=await api('/api/settings/capture',{method:'POST',body:JSON.stringify({values}),headers:{'Content-Type':'application/json'}});
    const result=await response.json(); if (!response.ok) throw new Error(result.error);
    captureValues=values; message('capture-message',t('参数已保存。下一次拍摄将使用新设置。'));
  } catch (error) { message('capture-message',t('保存未完成，请先结束录像或等待设备就绪。')+' '+(error.message||''),true); }
  finally {busy=false;$('capture-submit').disabled=false;refreshStatus(sessionGeneration);}
});
const arrival = new URLSearchParams(location.hash.slice(1));
if (arrival.has('setup')) {
  const token = arrival.get('setup');
  if (/^[a-f0-9]{64}$/.test(token)) setupTicket = token;
}
// A short-lived enrollment capability is transferred only in the fragment;
// remove it from the current history entry before any network request.
if (location.hash) history.replaceState(null, '', location.pathname);
function message(id, text, error = false) {
  bindTranslation($(id),text);
  $(id).classList.toggle('error', error);
}
async function api(path, options = {}) {
  const headers = {...options.headers};
  if (options.method === 'POST') headers['X-CSRF-Token'] = csrf;
  return fetch(path, {...options, headers, credentials: 'same-origin', cache: 'no-store'});
}
function stopPreview() {
  stopMediaVideo();
  previewing = false;
  $('preview').removeAttribute('src'); $('preview').hidden = true;
  $('preview-placeholder').hidden = false; $('preview-placeholder').textContent = t('点击开启实时预览');
  $('preview-toggle').textContent = t('开启预览');
  if (stillUrl) URL.revokeObjectURL(stillUrl);
  stillUrl = '';
}
function signedOut(text = t('请输入管理员密码。')) {
  ++sessionGeneration; csrf = ''; lastStatus = null;
  clearTimeout(polling); polling = null;
  stopPreview(); $('dashboard').hidden = true; $('login-panel').hidden = false;
  $('media-list').replaceChildren(); $('media-date').replaceChildren(); mediaCursor = datesCursor = '';
  mediaSelected.clear();updateMediaSelection();
  $('password').value = ''; $('login-button').disabled = false;
  $('network-password').value = ''; $('wg-config').value = ''; $('wg-file').value = '';
  $('wg-fields-form').reset();
  $('wg-mode-form').reset(); wgModeDirty = false; $('wg-route').textContent = '';
  $('account-form').reset();
  $('wg-counters').textContent = ''; $('wg-message').textContent = '';
  clearPrivateTranslation($('ble-message'));$('tl-files').replaceChildren();$('tl-sessions').replaceChildren();
  message('login-message', text);
}
async function signedIn(token) {
  csrf = token; ++sessionGeneration;
  $('password').value = ''; $('login-panel').hidden = true; $('dashboard').hidden = false;
  await refreshStatus(sessionGeneration);
  refreshNetwork();
  refreshMediaDates();
  refreshCaptureSettings();
  refreshTimelapseSessions();
}
function size(bytes) { return bytes >= 1073741824 ? (bytes / 1073741824).toFixed(1) + ' GB' : bytes >= 1048576 ? (bytes / 1048576).toFixed(1) + ' MB' : bytes >= 1024 ? Math.ceil(bytes / 1024) + ' KB' : bytes + ' B'; }
function time(seconds) {
  const hours = Math.floor(seconds / 3600), minutes = Math.floor(seconds % 3600 / 60);
  return hours ? t('{hours} 小时 {minutes} 分',{hours,minutes}) : t('{minutes} 分 {seconds} 秒',{minutes,seconds:seconds%60});
}
async function refreshStatus(generation) {
  if (document.hidden) return;
  try {
    const response = await api('/api/status');
    if (generation !== sessionGeneration) return;
    if (response.status === 401) { signedOut('登录已过期，请重新登录。'); return; }
    if (!response.ok) throw new Error('status');
    const status = await response.json();
    if (generation !== sessionGeneration) return;
    lastStatus = status;
    updateFeatures(status);
    refreshNetwork();
    refreshWireGuard();
    $('camera-state').textContent = status.cameraReady ? t('● 摄像机就绪') : t('摄像机暂不可用');
    $('record-state').hidden = !status.recording;
    const eventActive = status.events?.requested || status.events?.armed;
    const requested = status.recordRequested || status.recording || eventActive;
    $('record-start').hidden = requested; $('record-stop').hidden = eventActive || !requested;
    $('record-start').disabled = busy || !status.cameraReady || !status.sdReady;
    $('preview-toggle').disabled = !status.cameraReady;
    $('still').disabled = !status.cameraReady;
    $('photo').disabled = busy || eventActive || !status.cameraReady || !status.sdReady;
    $('event-start').hidden=Boolean(eventActive);$('event-stop').hidden=!eventActive;
    $('event-start').disabled=busy||requested||status.timelapse?.active||!status.cameraReady||!status.sdReady||!status.audioEnabled;
    $('event-stop').disabled=busy;$('event-sensitivity').disabled=Boolean(eventActive)||busy;
    const event = status.events;
    if(event) message('event-message',event.error!=='none'?t('抓拍状态：{error}',{error:event.error}):eventActive
      ?t(status.recording?(event.quietPending?'正在确认静止；空画面尾部不会保存。':'● 正在保存事件片段 · 480p'):'● 正在监测画面 · 480p')+' '+t('本次启动已保存 {count} 个事件。',{count:event.savedClips})
      :t('画面变化时保存独立片段；静止不保存。监测期间保持摄像头运行，退出登录后继续监测。'),event.error!=='none');
    for (const button of $('media-list').querySelectorAll('[data-delete],[data-rename]')) button.disabled = requested||mediaBusy;
    updateMediaSelection();
    for (const link of $('media-list').querySelectorAll('[data-video]')) link.setAttribute('aria-disabled', String(requested));
    $('restart').disabled = requested || busy;
    $('ota-submit').disabled = requested || busy || status.maintenance;
    $('temperature').textContent = `${status.temperatureC.toFixed(1)} °C`;
    $('sd-free').textContent = status.sdReady ? size(status.sdFreeBytes) : t('未就绪');
    $('sd-total').textContent = status.sdReady ? t('总容量 {size}',{size:size(status.sdTotalBytes)}) : t('检查卡是否插好');
    $('network').textContent = status.wifiConnected ? `${signal(status.rssi)} · ${status.rssi} dBm` : t('设备热点');
    $('lan-ip').textContent = status.wifiConnected ? status.lanIP : '192.168.4.1';
    $('uptime').textContent = time(status.uptimeSeconds);
    $('memory').textContent = t('可用内存 {heap} · PSRAM {psram}',{heap:size(status.freeHeap),psram:size(status.freePsram)});
    $('capture-format').textContent = `${status.videoWidth || 640} × ${status.videoHeight || 480} · ${status.fps} FPS · ${status.audioEnabled ? t('16 kHz 单声道') : t('麦克风已关闭')}`;
    const cpu=(status.cpu0Percent+status.cpu1Percent)/2;
    $('cpu-usage').textContent=status.cpuUsageAvailable ? cpu.toFixed(1)+'%' : t('正在采样');
    $('cpu-meter').value=status.cpuUsageAvailable ? cpu : 0;
    $('cpu-detail').textContent=status.cpuUsageAvailable ? `CPU 0 ${status.cpu0Percent.toFixed(1)}% · CPU 1 ${status.cpu1Percent.toFixed(1)}% · ${status.cpuMHz} MHz` : `${status.cpuMHz || 240} MHz`;
    const ram=status.totalHeap ? 100*(1-status.freeHeap/status.totalHeap) : 0;
    $('ram-usage').textContent=ram.toFixed(1)+'%';$('ram-meter').value=ram;
    $('ram-detail').textContent=t('内部堆 {used} / {total} · PSRAM {percent}%',{used:size(status.totalHeap-status.freeHeap),total:size(status.totalHeap),percent:status.totalPsram ? (100*(1-status.freePsram/status.totalPsram)).toFixed(1) : '—'});
    $('capture-submit').disabled=busy || requested || status.captureBusy || status.timelapse?.active;
    $('capture-defaults').disabled=busy || requested || status.timelapse?.active;
    for(const field of $('capture-fields').querySelectorAll('input,select'))field.disabled=Boolean(status.timelapse?.active);
    if(!status.timelapse?.active)updateManualFields();
    if (status.captureIdle) $('camera-state').textContent=t('● 待机省电，拍摄时自动唤醒');
    if (!status.cameraReady) { stopPreview(); message('action-message', status.maintenance ? t('固件更新中，摄像机已暂停。') : t('摄像机启动未完成。可以安全重启设备后再试。'), !status.maintenance); }
  } catch (_) {
    if (generation === sessionGeneration) message('action-message', t('设备连接中断，正在重试。'), true);
  } finally {
    if (generation === sessionGeneration && csrf && !document.hidden) polling = setTimeout(() => refreshStatus(generation), 2500);
  }
}
$('login-form').addEventListener('submit', async event => {
  event.preventDefault();
  if (busy) return;
  busy = true; $('login-button').disabled = true;
  let payload = JSON.stringify({password: $('password').value});
  $('password').value = '';
  message('login-message', t('正在验证密码，请稍候。240 MHz 时约需 35–50 秒；较低频率需要更久，设备保持可用。'));
  try {
    const response = await fetch('/api/auth/login', {method: 'POST', body: payload,
      headers: {'Content-Type': 'application/json', 'X-Appliance-Request': '1'},
      credentials: 'same-origin', cache: 'no-store'});
    payload = '';
    if (response.status === 429) throw new Error(t('尝试过于频繁，请 {seconds} 秒后重试。',{seconds:response.headers.get('Retry-After')||'60'}));
    if (response.status === 503) throw new Error(t('设备正在处理另一项登录请求，请稍后重试。'));
    if (response.status === 409) throw new Error(t('设备首次设置尚未完成。'));
    if (response.status !== 202) throw new Error(t('登录请求未被接受，请重试。'));
    let ticket = (await response.json()).ticket;
    const deadline = Date.now() + 280000;
    while (Date.now() < deadline) {
      await new Promise(resolve => setTimeout(resolve, 1000));
      const result = await fetch('/api/auth/result', {headers: {'X-Login-Ticket': ticket}, credentials: 'same-origin', cache: 'no-store'});
      if (result.status === 202) continue;
      ticket = '';
      if (!result.ok) throw new Error(t('密码不正确，或登录请求已失效。'));
      await signedIn((await result.json()).csrf); return;
    }
    throw new Error(t('验证等待超时，请稍后重试。'));
  } catch (error) { message('login-message', error.message || t('无法连接设备，请重试。'), true); }
  finally { payload = ''; busy = false; $('login-button').disabled = false; }
});
$('account-form').addEventListener('submit', async event => {
  event.preventDefault(); if (busy || !csrf) return;
  let current=$('account-current').value, replacement=$('account-new').value, confirmation=$('account-confirm').value;
  if (Array.from(replacement).length < 10 || new TextEncoder().encode(replacement).length > 128) {
    message('account-message',t('管理员密码需至少 10 个字符，最多 128 个 UTF-8 字节。'),true);return;
  }
  if (replacement !== confirmation || replacement === current) {
    message('account-message',t('两次新密码需一致，且与当前密码不同。'),true);return;
  }
  const generation=sessionGeneration; busy=true; $('account-submit').disabled=true;
  let payload=JSON.stringify({currentPassword:current,newPassword:replacement,confirmPassword:confirmation}),ticket='';
  current=replacement=confirmation=''; $('account-form').reset();
  message('account-message',t('正在验证并保存新密码，请稍候。较低频率需要更久，设备保持可用。'));
  try {
    const response=await api('/api/account/password',{method:'POST',body:payload,headers:{'Content-Type':'application/json'}});
    payload='';
    if(response.status===429)throw new Error(t('尝试过于频繁，请 {seconds} 秒后重试。',{seconds:response.headers.get('Retry-After')||'60'}));
    if(response.status!==202)throw new Error(t('密码修改未被接受，请检查输入或等待当前操作完成。'));
    ticket=(await response.json()).ticket;
    const deadline=Date.now()+550000;
    while(Date.now()<deadline) {
      await new Promise(resolve=>setTimeout(resolve,1000));
      const result=await fetch('/api/account/password/result',{headers:{'X-Appliance-Request':'1','X-Password-Change-Ticket':ticket},
        credentials:'same-origin',cache:'no-store'});
      if(result.status===202)continue;
      ticket='';const body=await result.json();
      if(result.ok&&body.changed) {
        if(generation===sessionGeneration||!csrf)signedOut(t('密码已修改，请用新密码重新登录。'));
        return;
      }
      throw new Error(t(body.error==='invalid_credentials'?'当前密码不正确。':'密码修改未能确认，请重新登录检查结果。'));
    }
    throw new Error(t('等待超时，请重新登录确认密码修改结果。'));
  } catch(error) {
    if(generation===sessionGeneration)message('account-message',error.message||t('无法连接设备，请重试。'),true);
  } finally {payload=ticket='';busy=false;$('account-submit').disabled=false;}
});
function showSetup() {
  $('login-panel').hidden = true; $('dashboard').hidden = true; $('setup-panel').hidden = false;
}
async function setupPoll() {
  if (!setupTicket) return;
  try {
    const response = await api('/api/setup/result', {headers: {'X-Setup-Ticket': setupTicket, 'X-Appliance-Request': '1'}});
    if (!response.ok) {
      setupTicket = ''; $('setup-confirm').hidden = true; $('setup-lan-link').hidden = true;
      throw new Error(t('设置请求已失效。若账户尚未建立，请连接设备热点后刷新重试。'));
    }
    const state = await response.json();
    setupRevision = state.revision;
    if (state.phase === 'active') {
      setupTicket = ''; $('setup-panel').hidden = true;
      signedOut('首次设置已完成，请使用你选定的管理员密码登录。'); return;
    }
    if (state.phase === 'failed') {
      setupTicket = ''; $('setup-submit').disabled = false; $('setup-form').hidden = false;
      $('setup-confirm').hidden = true; $('setup-lan-link').hidden = true;
      message('setup-message', t('连接未完成，候选配置未启用。请检查 Wi-Fi 后重新设置。'), true); return;
    }
    if (state.phase === 'awaiting_local_confirmation') {
      message('setup-message', t('Wi-Fi 已连接。请在 90 秒试运行期限内，从新的局域网地址确认。'));
      if (location.hostname === state.lanIP) $('setup-confirm').hidden = false;
      else {
        $('setup-lan-link').href = `http://${state.lanIP}/#setup=${setupTicket}`;
        $('setup-lan-link').textContent = t('切回家庭 Wi-Fi，再打开 http://{address}/ 确认',{address:state.lanIP});
        $('setup-lan-link').hidden = false;
      }
    } else message('setup-message', state.phase === 'hashing' ? t('正在建立密码校验值，约需 35–50 秒。设备保持响应。') : t('正在连接 Wi-Fi，热点会继续保留。'));
    setupPolling = setTimeout(setupPoll, 1000);
  } catch (error) {
    message('setup-message', error.message || t('连接中断，请检查网络。'), true);
    if (setupTicket) setupPolling = setTimeout(setupPoll, 2000);
  }
}
$('setup-form').addEventListener('submit', async event => {
  event.preventDefault();
  if (busy || setupTicket) return;
  let password = $('setup-password').value;
  if (Array.from(password).length < 10 || new TextEncoder().encode(password).length > 128) {
    message('setup-message', t('管理员密码需至少 10 个字符，最多 128 个 UTF-8 字节。'), true); return;
  }
  busy = true; $('setup-submit').disabled = true;
  let payload = JSON.stringify({ssid: $('setup-ssid').value, wifiPassword: $('setup-wifi-password').value, adminPassword: password});
  password = '';
  $('setup-password').value = ''; $('setup-wifi-password').value = '';
  try {
    const response = await api('/api/setup', {method:'POST', body:payload,
      headers:{'Content-Type':'application/json', 'X-Appliance-Request':'1', 'X-Setup-Nonce':setupNonce}});
    payload = '';
    if (response.status !== 202) throw new Error(t('设置未被接受，请检查输入或等待当前试运行结束。'));
    setupTicket = (await response.json()).ticket; $('setup-form').hidden = true; setupPoll();
  } catch (error) { message('setup-message', error.message, true); $('setup-submit').disabled = false; }
  finally { payload = ''; busy = false; }
});
$('setup-confirm').addEventListener('click', async () => {
  $('setup-confirm').disabled = true;
  try {
    const response = await api('/api/setup/confirm', {method:'POST', body:JSON.stringify({revision:setupRevision}),
      headers:{'Content-Type':'application/json', 'X-Appliance-Request':'1', 'X-Setup-Ticket':setupTicket}});
    if (response.status !== 202) throw new Error(t('确认未被接受。请从新 Wi-Fi 的局域网地址打开此页面。'));
    message('setup-message', t('正在确认并保存配置。'));
  } catch (error) { message('setup-message', error.message, true); }
  finally { $('setup-confirm').disabled = false; }
});
async function refreshNetwork() {
  if (!csrf) return;
  try {
    const response = await api('/api/network');
    if (!response.ok) return;
    const state = await response.json();
    if (state.revision && Number($('network-confirm').dataset.revision) !== state.revision)
      $('network-confirm').closest('details').open = true;
    $('network-cancel').hidden = !state.revision;
    $('network-confirm').hidden = state.state !== 'awaiting_local_confirmation' || location.hostname !== state.lanIP;
    $('network-confirm').dataset.revision = state.revision;
    if (state.revision && state.wifiConnected && location.hostname !== state.lanIP) {
      $('network-lan-link').href = `http://${state.lanIP}/`;
      $('network-lan-link').textContent = t('切回新的 Wi-Fi，再登录 http://{address}/ 确认',{address:state.lanIP});
      $('network-lan-link').hidden = false;
    } else $('network-lan-link').hidden = true;
    message('network-message', state.state === 'ready' ? t('当前网络已确认。') : state.state === 'awaiting_local_confirmation' ? t('新网络等待局域网确认，逾期自动回退。') : state.revision ? t('正在测试新网络。') : state.error === 'connect_timeout' ? t('新网络连接失败，已恢复之前的配置。') : t('设备正在连接或恢复网络。'));
  } catch (_) { message('network-message', t('网络连接中断，正在等待设备恢复。'), true); }
}
$('network-form').addEventListener('submit', async event => {
  event.preventDefault(); if (busy) return; busy = true; $('network-submit').disabled = true;
  let payload = JSON.stringify({ssid:$('network-ssid').value, wifiPassword:$('network-password').value});
  $('network-password').value = '';
  try {
    const response = await api('/api/network/trial', {method:'POST', body:payload, headers:{'Content-Type':'application/json'}});
    payload = ''; if (response.status !== 202) throw new Error(t('新网络试运行未被接受，请检查输入或等待当前操作完成。'));
    message('network-message', t('新网络开始试运行。请连接成功后从新的局域网地址确认。'));
  } catch (error) { message('network-message', error.message, true); }
  finally { payload = ''; busy = false; $('network-submit').disabled = false; }
});
$('network-confirm').addEventListener('click', async () => {
  try {
    const response = await api('/api/network/confirm', {method:'POST', body:JSON.stringify({revision:Number($('network-confirm').dataset.revision)}), headers:{'Content-Type':'application/json'}});
    if (response.status !== 202) throw new Error('confirm'); refreshNetwork();
  } catch (_) { message('network-message', t('网络确认未成功，请检查访问地址。'), true); }
});
$('network-cancel').addEventListener('click', async () => {
  try { await api('/api/network/cancel', {method:'POST'}); refreshNetwork(); }
  catch (_) { message('network-message', t('取消操作未确认。'), true); }
});
async function refreshWireGuard() {
  if (!csrf) return;
  const generation = sessionGeneration;
  try {
    const response = await api('/api/wireguard'); if (!response.ok) return;
    const wg = await response.json();
    if (generation !== sessionGeneration) return;
    const phase = {disabled:t('已关闭'), waiting_wifi:t('等待 Wi-Fi'), waiting_time:t('等待时间同步'), resolving_endpoint:t('正在解析服务器'), starting:t('正在启动'), handshaking:t('正在建立会话'), peer_session_up:t('远程访问会话已建立'), verifying_full_tunnel:t('正在验证全隧道出口'), full_tunnel_verified:t('全隧道出口已验证'), retry_wait:t('等待重试'), error:t('启动失败')}[wg.phase] || t('正在连接');
    const knownErrors = {full_tunnel_prerequisites_missing:'全隧道所需配置不完整，请检查默认路由、DNS 和 MTU。', full_tunnel_guard_unavailable:'公网出口保护未就绪，请取消试运行后重试。', endpoint_bootstrap_dns_unavailable:'服务器名称解析暂不可用，正在等待重试。', full_tunnel_public_probe_failed:'出口验证未通过，正在重试。请检查服务器转发和 NAT。'};
    const error = wg.importError !== 'none' && wg.importError !== 'ok' ? wg.importError : wg.error;
    message('wg-message', phase + (error !== 'none' ? ' · ' + (knownErrors[error] ? t(knownErrors[error]) : error) : ''), error !== 'none');
    if (!wgModeDirty) $('wg-mode').value = wg.mode;
    message('wg-route', wg.mode === 'full_tunnel' ? (wg.fullTunnelVerified
      ? t('公网出口 {address} · 验证于 {seconds} 秒前',{address:wg.publicExit,seconds:wg.verificationAgeSeconds})
      : t('全隧道已选择，正在等待出口验证。')) : t('当前为远程访问模式，普通上网使用 Wi-Fi。'));
    $('wg-counters').textContent = wg.running ? t('设备地址 {address} · 已发送 {sent} · 已接收 {received}',{address:wg.address,sent:size(wg.sentBytes),received:size(wg.receivedBytes)}) : '';
    $('wg-submit').disabled = busy || Boolean(wg.trialRevision) || Boolean(lastStatus?.maintenance);
    $('wg-fields-submit').disabled = $('wg-submit').disabled;
    $('wg-disable').disabled = busy || !wg.enabled || Boolean(wg.trialRevision) || Boolean(lastStatus?.maintenance);
    $('wg-restart').disabled = $('wg-disable').disabled;
    $('wg-mode-submit').disabled = $('wg-disable').disabled || location.hostname !== lastStatus?.lanIP;
    $('wg-mode').disabled = busy || Boolean(wg.trialRevision) || Boolean(lastStatus?.maintenance);
    $('wg-clear').disabled = busy || !wg.privateKeySet || Boolean(wg.trialRevision) || Boolean(lastStatus?.maintenance);
  } catch (_) { message('wg-message', t('暂时无法读取 WireGuard 状态。'), true); }
}
$('wg-mode').addEventListener('change', () => { wgModeDirty = true; });
$('wg-mode-form').addEventListener('submit', async event => {
  event.preventDefault(); if (busy) return; busy = true; $('wg-mode-submit').disabled = true;
  try {
    const response = await api('/api/wireguard/mode', {method:'POST',body:JSON.stringify({mode:$('wg-mode').value}),headers:{'Content-Type':'application/json'}});
    if (response.status !== 202) throw new Error('mode');
    wgModeDirty = false;
    message('network-message', t('连接模式开始试运行。出口验证通过后，请在下方确认网络。'));
  } catch (_) { message('wg-message', t('模式试运行未被接受。请从局域网访问，并等待当前操作完成。'), true); }
  finally { busy = false; refreshWireGuard(); refreshNetwork(); }
});
$('wg-file').addEventListener('change', async () => {
  const file = $('wg-file').files[0];
  if (!file || file.size > 4096) { message('wg-message', t('配置文件须为不超过 4 KB 的单个 peer。'), true); return; }
  const generation = sessionGeneration;
  try {
    let text = await file.text();
    if (generation === sessionGeneration && csrf) $('wg-config').value = text;
    text = '';
  } catch (_) { if (generation === sessionGeneration) message('wg-message', t('无法读取所选文件。'), true); }
});
$('wg-form').addEventListener('submit', async event => {
  event.preventDefault(); if (busy) return;
  let config = $('wg-config').value; $('wg-config').value = ''; $('wg-file').value = '';
  await importWireGuard(config); config = '';
});
$('wg-fields-form').addEventListener('submit', async event => {
  event.preventDefault(); if (busy) return;
  const value = id => $('wg-' + id).value.trim();
  const names = ['address','private-key','public-key','preshared-key','endpoint','port','allowed-ips','dns','mtu','keepalive'];
  if (names.some(id => /[\r\n\x00]/.test(value(id)))) { message('wg-message', t('每个字段请填写单行内容。'), true); return; }
  let config = '[Interface]\nPrivateKey = ' + value('private-key') + '\nAddress = ' + value('address')
    + '\nMTU = ' + value('mtu') + (value('dns') ? '\nDNS = ' + value('dns') : '')
    + '\n[Peer]\nPublicKey = ' + value('public-key')
    + (value('preshared-key') ? '\nPresharedKey = ' + value('preshared-key') : '')
    + '\nEndpoint = ' + value('endpoint') + ':' + value('port')
    + '\nAllowedIPs = ' + value('allowed-ips') + '\nPersistentKeepalive = ' + value('keepalive') + '\n';
  $('wg-fields-form').reset();
  await importWireGuard(config); config = '';
});
async function importWireGuard(config) {
  busy = true; $('wg-submit').disabled = $('wg-fields-submit').disabled = true;
  try {
    const response = await api('/api/wireguard/import', {method:'POST', body:config, headers:{'Content-Type':'text/plain; charset=utf-8'}});
    config = ''; if (response.status !== 202) throw new Error('import');
    message('wg-message', t('配置已接收，正在试运行。会话成功后请从局域网确认。'));
  } catch (_) { message('wg-message', t('导入未完成，请检查配置或等待当前操作结束。'), true); }
  finally { config = ''; busy = false; refreshWireGuard(); refreshNetwork(); }
}
$('wg-restart').addEventListener('click', async () => {
  if (busy) return; busy = true;
  try {
    const response = await api('/api/wireguard/restart', {method:'POST'});
    if (response.status !== 202) throw new Error('restart');
    message('wg-message', t('正在重新建立远程连接。'));
  } catch (_) { message('wg-message', t('重连操作未被接受，请等待当前网络操作完成。'), true); }
  finally { busy = false; refreshWireGuard(); }
});
$('wg-clear').addEventListener('click', async () => {
  if (busy || !confirm(t('清除已保存的 WireGuard 配置？请保留你的原始配置文件。此操作先试运行，局域网确认后才保存。'))) return;
  busy = true;
  try {
    const response = await api('/api/wireguard/clear', {method:'POST'});
    if (response.status !== 202) throw new Error('clear'); refreshNetwork();
    message('wg-message', t('清除配置的试运行已开始，请从局域网确认。'));
  } catch (_) { message('wg-message', t('清除试运行未被接受。'), true); }
  finally { busy = false; refreshWireGuard(); }
});
$('wg-disable').addEventListener('click', async () => {
  if (busy || !confirm(t('测试关闭 WireGuard？确认前，逾期会恢复当前配置。'))) return;
  try {
    const response = await api('/api/wireguard/disable', {method:'POST'});
    if (response.status !== 202) throw new Error('disable'); refreshWireGuard(); refreshNetwork();
  } catch (_) { message('wg-message', t('关闭试运行未被接受。'), true); }
});
$('logout').addEventListener('click', async () => {
  try {
    const response = await api('/api/auth/logout', {method: 'POST'});
    if (!response.ok) throw new Error('logout');
    signedOut('已退出登录。');
  } catch (_) { stopPreview(); message('action-message', t('未能确认退出，请恢复连接后重试。'), true); }
});
$('preview-toggle').addEventListener('click', () => {
  if (previewing) { stopPreview(); return; }
  stopPreview(); previewing = true;
  $('preview').src = '/api/camera/stream'; $('preview').hidden = false;
  $('preview-placeholder').hidden = true; $('preview-toggle').textContent = t('关闭预览');
});
$('preview').addEventListener('error', () => { stopPreview(); message('action-message', t('预览连接已关闭。请检查设备状态后重试。'), true); });
$('still').addEventListener('click', async () => {
  stopPreview();
  try {
    const response = await api('/api/camera/still');
    if (response.status === 401) { signedOut('请重新登录。'); return; }
    if (!response.ok) throw new Error('still');
    const blob = await response.blob();
    if (!csrf) return;
    stillUrl = URL.createObjectURL(blob); $('preview').src = stillUrl; $('preview').hidden = false;
    $('preview-placeholder').hidden = true; message('action-message', t('当前单张画面已显示。'));
  } catch (_) { message('action-message', t('暂时无法获取画面，请重试。'), true); }
});
$('photo').addEventListener('click', async () => {
  if (busy) return; busy = true; $('photo').disabled = true;
  try {
    const response = await api('/api/camera/photo', {method:'POST'});
    if (!response.ok) throw new Error('photo');
    const saved = await response.json();
    message('action-message', saved.timeSynchronized ? t('照片已保存到 SD 卡。') : t('照片已保存；设备时间尚未同步，文件日期可能不准确。'));
    refreshMediaDates(saved.path.slice(1,9));
  } catch (_) { message('action-message', t('照片未能保存，请检查摄像机和 SD 卡。'), true); }
  finally { busy = false; $('photo').disabled = false; }
});
async function refreshMediaDates(selected = '', append = false) {
  if (!csrf || mediaBusy) return; mediaBusy = true;
  const generation = sessionGeneration;
  try {
    const response = await api('/api/media/dates' + (append && datesCursor ? '?before=' + datesCursor : ''));
    if (!response.ok) throw new Error('media');
    const page = await response.json(); if (generation !== sessionGeneration) return;
    const previous = selected || $('media-date').value;
    if (!append) {$('media-date').replaceChildren();$('media-list').replaceChildren();mediaSelected.clear();}
    for (const item of page.items) {
      const option = document.createElement('option'); option.value = item.date;
      option.textContent = `${item.date.slice(0,4)}-${item.date.slice(4,6)}-${item.date.slice(6,8)}`;
      $('media-date').append(option);
    }
    if (Array.from($('media-date').options).some(option => option.value === previous)) $('media-date').value = previous;
    datesCursor = page.nextBefore; $('media-older-dates').hidden = !page.more;
    message('media-message', page.enumerationTruncated ? t('目录较多，本次扫描已达到上限。') : page.items.length ? t('正在加载媒体。') : t('还没有保存的照片或录像。'));
  } catch (_) { message('media-message', t('暂时无法读取 SD 卡媒体。'), true); }
  finally { mediaBusy = false;updateMediaSelection(); }
  if (generation === sessionGeneration && $('media-date').value && !append) await refreshMedia();
}
async function refreshMedia(append = false) {
  if (!csrf || mediaBusy || !$('media-date').value) return; mediaBusy = true;
  const generation = sessionGeneration;
  try {
    const response = await api('/api/media?date=' + $('media-date').value + (append && mediaCursor ? '&before=' + encodeURIComponent(mediaCursor) : ''));
    if (!response.ok) throw new Error('media');
    const page = await response.json(); if (generation !== sessionGeneration) return;
    if (!append) {$('media-list').replaceChildren();mediaSelected.clear();}
    for (const item of page.items) {
      const row = document.createElement('li'), info = document.createElement('div'), actions = document.createElement('div');
      row.dataset.path=item.path;
      const choice=document.createElement('input');choice.type='checkbox';choice.dataset.mediaChoice=item.path;
      choice.setAttribute('aria-label',t('选择 {name}',{name:item.name}));choice.checked=mediaSelected.has(item.path);
      choice.addEventListener('change',()=>{if(choice.checked)mediaSelected.add(item.path);else mediaSelected.delete(item.path);updateMediaSelection();});
      info.className = 'media-info'; actions.className = 'media-actions';
      const name = document.createElement('strong'), detail = document.createElement('span');
      name.textContent = item.name; detail.textContent = `${item.kind === 'photo' ? t('照片') : t('带声音的录像')} · ${size(item.bytes)}`;
      info.append(name, detail);
      if (item.kind === 'photo') {
        const view = document.createElement('button'); view.type = 'button'; view.textContent = t('预览');
        view.addEventListener('click', () => { stopPreview(); $('preview').src = '/api/media/file?path=' + encodeURIComponent(item.path); $('preview').hidden = false; $('preview-placeholder').hidden = true; $('preview').scrollIntoView({block:'center', behavior:'smooth'}); });
        actions.append(view);
      }
      if(item.name.endsWith('.mp4')){
        const view=document.createElement('button');view.type='button';view.textContent=t('播放 MP4');
        view.addEventListener('click',()=>{stopPreview();$('preview').hidden=true;$('preview-placeholder').hidden=true;
          $('media-video').src='/api/media/file?path='+encodeURIComponent(item.path);$('media-video').hidden=false;
          $('media-video').play().catch(()=>message('media-message',t('请点击播放器开始播放。')));});actions.append(view);
      }
      const download = document.createElement('a'); download.href = '/api/media/download?path=' + encodeURIComponent(item.path); download.textContent = t('下载');
      if (item.kind === 'video') { download.dataset.video = 'true'; download.setAttribute('aria-disabled', String(Boolean(lastStatus?.recording || lastStatus?.recordRequested))); }
      const rename=document.createElement('button');rename.type='button';rename.textContent=t('改名');rename.dataset.rename='true';
      rename.disabled=mediaMutationsBlocked();
      rename.addEventListener('click',async()=>{
        const label=prompt(t('新名称（无需扩展名，保留拍摄日期前缀；最多 64 个 UTF-8 字节）'),item.name.slice(16,-4));
        if(label===null)return;rename.disabled=true;
        try {
          const response=await api('/api/media/rename',{method:'POST',body:JSON.stringify({path:item.path,name:label}),headers:{'Content-Type':'application/json'}});
          const result=await response.json();if(!response.ok)throw new Error(result.error);
          message('media-message',t('文件已改名。'));await refreshMedia();
        } catch(error){message('media-message',t('改名未完成。请使用文字、数字、空格、下划线或连字符，并先结束录像或下载。'),true);}
        finally{rename.disabled=false;}
      });
      const remove = document.createElement('button'); remove.type = 'button'; remove.textContent = t('删除'); remove.dataset.delete = 'true';
      remove.disabled = mediaMutationsBlocked();
      remove.addEventListener('click', async () => {
        if (!confirm(t('永久删除 {name}？',{name:item.name}))) return;
        remove.disabled = true;
        try {
          const response = await api('/api/media/delete', {method:'POST', body:JSON.stringify({path:item.path}), headers:{'Content-Type':'application/json'}});
          if (!response.ok) throw new Error('delete'); row.remove();mediaSelected.delete(item.path);updateMediaSelection(); message('media-message', t('文件已删除。'));
        } catch (_) { remove.disabled = false; message('media-message', t('删除未完成。请先结束录像或下载后再试。'), true); }
      });
      const entry=document.createElement('div');entry.className='media-entry';entry.append(choice,info);
      actions.append(download,rename,remove); row.append(entry, actions); $('media-list').append(row);
    }
    mediaCursor = page.nextBefore; $('media-more').hidden = !page.more;
    message('media-message', page.enumerationTruncated ? t('本次扫描达到上限，部分文件可能尚未列出。') : $('media-list').children.length ? t('已加载 SD 卡媒体。') : t('这一天还没有媒体文件。'));
  } catch (_) { message('media-message', t('暂时无法加载文件。'), true); }
  finally { mediaBusy = false;updateMediaSelection(); }
}
function mediaMutationsBlocked() {
  return Boolean(mediaBusy||busy||lastStatus?.recording||lastStatus?.recordRequested||lastStatus?.events?.requested||lastStatus?.events?.armed||lastStatus?.maintenance);
}
function updateMediaSelection() {
  const choices=Array.from($('media-list').querySelectorAll('[data-media-choice]'));
  const selected=choices.filter(input=>input.checked).length;
  $('media-select-all').checked=choices.length>0&&selected===choices.length;
  $('media-select-all').indeterminate=selected>0&&selected<choices.length;
  $('media-select-all').disabled=mediaBusy||!choices.length;
  $('media-selection-count').textContent=t('已选择 {count} 个文件',{count:mediaSelected.size});
  $('media-delete-selected').disabled=mediaMutationsBlocked()||!mediaSelected.size;
  $('media-delete-all').disabled=mediaMutationsBlocked()||!lastStatus?.sdReady;
  for(const input of choices)input.disabled=mediaBusy;
  for(const button of $('media-list').querySelectorAll('[data-delete],[data-rename]'))button.disabled=mediaMutationsBlocked();
  for(const id of ['media-refresh','media-date','media-more','media-older-dates'])$(id).disabled=mediaBusy;
}
$('media-select-all').addEventListener('change',event=>{
  for(const input of $('media-list').querySelectorAll('[data-media-choice]')) {
    input.checked=event.target.checked;if(input.checked)mediaSelected.add(input.dataset.mediaChoice);else mediaSelected.delete(input.dataset.mediaChoice);
  }
  updateMediaSelection();
});
async function allMediaPaths() {
  const paths=new Set(),dates=[];
  const pages=async(base,collect)=>{
    let cursor='';const seen=new Set();
    for(let pageNumber=0;pageNumber<1000;++pageNumber) {
      const response=await api(base+(cursor?(base.includes('?')?'&':'?')+'before='+encodeURIComponent(cursor):''));
      if(!response.ok)throw new Error('enumeration');
      const page=await response.json();
      if(page.enumerationTruncated||!Array.isArray(page.items))throw new Error('incomplete');
      collect(page.items);
      if(!page.more)return;
      if(!page.nextBefore||seen.has(page.nextBefore))throw new Error('cursor');
      cursor=page.nextBefore;seen.add(cursor);
    }
    throw new Error('page_limit');
  };
  await pages('/api/media/dates',items=>{for(const item of items)dates.push(item.date);});
  for(const date of dates)await pages('/api/media?date='+encodeURIComponent(date),items=>{
    for(const item of items){paths.add(item.path);if(paths.size>50000)throw new Error('file_limit');}
  });
  return Array.from(paths);
}
async function bulkDelete(all) {
  if(mediaMutationsBlocked())return;
  const generation=sessionGeneration;
  mediaBusy=true;busy=true;updateMediaSelection();let completed=0,outcome='',failed=false;
  stopPreview();
  try {
    const paths=all?await allMediaPaths():Array.from(mediaSelected);
    if(generation!==sessionGeneration)return;
    if(!paths.length){message('media-message',t('没有可删除的文件。'));return;}
    if(!confirm(t(all?'永久删除全部日期的 {count} 张照片和录像？此操作无法撤销。':'永久删除所选的 {count} 个文件？此操作无法撤销。',{count:paths.length})))return;
    for(let i=0;i<paths.length;i+=20) {
      if(generation!==sessionGeneration)throw new Error('session');
      const response=await api('/api/media/delete-batch',{method:'POST',body:JSON.stringify({paths:paths.slice(i,i+20)}),headers:{'Content-Type':'application/json'}});
      const result=await response.json();completed+=Number(result.deleted)||0;
      message('media-message',t('已删除 {count} / {total} 个文件',{count:completed,total:paths.length}));
      if(!response.ok||result.error!=='none')throw new Error('delete');
    }
    outcome=t('已删除 {count} 个文件。',{count:completed});
  } catch(_) {
    failed=true;outcome=completed?t('删除已停止，已删除 {count} 个文件。请刷新检查剩余文件。',{count:completed}):t('删除未开始或未完成。请停止录像和抓拍、结束下载，并检查 SD 卡；目录扫描不完整时不会开始清空。');
  } finally {
    mediaBusy=false;busy=false;
    if(generation===sessionGeneration&&csrf){if(completed){mediaSelected.clear();await refreshMediaDates();}if(outcome)message('media-message',outcome,failed);}
    updateMediaSelection();
  }
}
$('media-delete-selected').addEventListener('click',()=>bulkDelete(false));
$('media-delete-all').addEventListener('click',()=>bulkDelete(true));
$('media-refresh').addEventListener('click', () => refreshMediaDates());
$('media-date').addEventListener('change', () => refreshMedia());
$('media-older-dates').addEventListener('click', () => refreshMediaDates('', true));
$('media-more').addEventListener('click', () => refreshMedia(true));
async function mutate(path, text) {
  if (busy) return;
  busy = true;
  try {
    const response = await api(path, {method: 'POST'});
    if (response.status === 403) { signedOut('登录已失效，请重新登录。'); return; }
    if (!response.ok) throw new Error('mutation');
    message('action-message', text);
  } catch (_) { message('action-message', t('操作未能确认，请检查设备状态后重试。'), true); }
  finally { busy = false; }
}
$('record-start').addEventListener('click', () => mutate('/api/record/start', t('正在开始录制，视频和声音将保存到 SD 卡。')));
$('record-stop').addEventListener('click', () => mutate('/api/record/stop', t('正在结束录制并保存，请稍候。')));
$('event-start').addEventListener('click',async()=>{
  if(busy)return;busy=true;
  try {
    const response=await api('/api/events/start',{method:'POST',body:JSON.stringify({sensitivity:Number($('event-sensitivity').value)}),headers:{'Content-Type':'application/json'}});
    if(response.status!==202)throw new Error('event');
    message('action-message',t('已请求开启 480p 事件监测。'));
  } catch(_){message('action-message',t('抓拍未能开启，请先结束录像和下载，并检查麦克风与 SD 卡。'),true);}
  finally {busy=false;}
});
$('event-stop').addEventListener('click',()=>mutate('/api/events/stop',t('正在停止监测并保存当前事件。')));
$('restart').addEventListener('click', async () => {
  if (!confirm(t('确认重启设备？连接会短暂中断。'))) return;
  await mutate('/api/device/restart', t('设备正在重启，请稍后刷新页面。'));
  stopPreview();
});
$('ota-digest-file').addEventListener('change', async () => {
  const file = $('ota-digest-file').files[0];
  if (!file || file.size > 256) { message('ota-message', t('请选择配套的小型校验文件。'), true); return; }
  const value = (await file.text()).trim().split(/\s+/)[0].toLowerCase();
  if (!/^[a-f0-9]{64}$/.test(value)) { message('ota-message', t('校验文件格式无效。'), true); return; }
  $('ota-digest').value = value; message('ota-message', t('已读取 SHA-256 校验值。'));
});
$('ota-form').addEventListener('submit', async event => {
  event.preventDefault(); if (busy || otaUploading) return;
  const file = $('ota-file').files[0], digest = $('ota-digest').value.trim().toLowerCase();
  if (!file || !/^[a-f0-9]{64}$/.test(digest)) return;
  try {
    const response = await api('/api/ota');
    if (!response.ok) throw new Error(t('无法读取更新状态。'));
    const limits = await response.json();
    if (file.size < 288 || file.size > limits.maximumBytes) throw new Error(t('固件大小超出设备允许范围。'));
    if (limits.maintenance) throw new Error(t('设备已经在更新，请等待完成。'));
    if (!confirm(t('确认安装所选固件？摄像机将暂停，完成后设备重启。'))) return;
  } catch (error) { message('ota-message', error.message, true); return; }
  busy = otaUploading = true; stopPreview(); $('ota-submit').disabled = true;
  $('ota-progress').hidden = false; $('ota-progress').value = 0;
  message('ota-message', t('正在上传固件，请保持连接。'));
  const request = new XMLHttpRequest();
  request.open('POST', '/api/ota'); request.timeout = 310000;
  request.setRequestHeader('Content-Type', 'application/octet-stream');
  request.setRequestHeader('X-CSRF-Token', csrf); request.setRequestHeader('X-Firmware-SHA256', digest);
  request.upload.addEventListener('progress', event => {
    if (!event.lengthComputable) return;
    const percent = Math.floor(event.loaded * 100 / event.total);
    $('ota-progress').value = percent;
    message('ota-message', percent < 100 ? t('正在上传：{percent}%',{percent}) : t('上传完成，正在校验并准备重启。'));
  });
  request.addEventListener('loadend', () => {
    busy = otaUploading = false; $('ota-file').value = ''; $('ota-submit').disabled = false;
    let result = {}; try { result = JSON.parse(request.responseText); } catch (_) {}
    if (request.status === 200 && result.updated) signedOut('固件更新成功，设备正在重启。稍后请重新登录。');
    else if (result.restarting) signedOut('更新未通过校验，设备将重启回当前固件。稍后请重新登录。');
    else message('ota-message', result.error ? t('更新未完成：') + result.error : t('连接中断。请等待设备恢复并检查状态后重试。'), true);
  });
  request.send(file);
});
document.addEventListener('visibilitychange', () => {
  clearTimeout(polling); polling = null;
  if (document.hidden) stopPreview();
  else if (csrf) refreshStatus(sessionGeneration);
});
window.addEventListener('pagehide', stopPreview);
window.addEventListener('pagehide', () => { clearTimeout(setupPolling); setupTicket = ''; setupNonce = ''; });
document.addEventListener('cameralanguagechange',()=>{
  $('preview-toggle').textContent=t(previewing?'关闭预览':'开启预览');
  if(csrf){clearTimeout(polling);polling=null;refreshStatus(sessionGeneration);refreshMedia();refreshTimelapseSessions();}
});
(async () => {
  try {
    if (setupTicket) { showSetup(); $('setup-form').hidden = true; setupPoll(); return; }
    const response = await api('/api/auth/session');
    if (response.ok) { await signedIn((await response.json()).csrf); return; }
    const bootstrap = await (await api('/api/bootstrap')).json();
    if (!bootstrap.adminConfigured) {
      if (bootstrap.setupNonce) { setupNonce = bootstrap.setupNonce; showSetup(); message('setup-message', t('请输入 Wi-Fi 和管理员密码。')); }
      else { $('login-button').disabled = true; message('login-message', t('请连接设备热点后完成首次设置；若存储故障，请检查设备。')); }
    } else message('login-message', t('请输入管理员密码。'));
  } catch (_) { message('login-message', t('无法连接设备，请检查网络后刷新。'), true); }
})();
