var sha256 = (function () {
	var K = new Int32Array([0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2]);
	function rr(v, n) { return (v >>> n) | (v << (32 - n)); }
	return function (buf) {
		var H = new Int32Array([0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19]);
		var bytes = new Uint8Array(buf);
		var len = bytes.length;
		var bh = len >>> 29, bl = (len << 3) >>> 0;
		var padLen = ((len + 9 + 63) & ~63);
		var padded = new Uint8Array(padLen);
		padded.set(bytes);
		padded[len] = 0x80;
		padded[padLen-8]=(bh>>>24)&0xff;padded[padLen-7]=(bh>>>16)&0xff;padded[padLen-6]=(bh>>>8)&0xff;padded[padLen-5]=bh&0xff;
		padded[padLen-4]=(bl>>>24)&0xff;padded[padLen-3]=(bl>>>16)&0xff;padded[padLen-2]=(bl>>>8)&0xff;padded[padLen-1]=bl&0xff;
		var dv = new DataView(padded.buffer);
		var W = new Int32Array(64);
		for (var off = 0; off < padLen; off += 64) {
			for (var t = 0; t < 16; t++) W[t] = dv.getInt32(off + t * 4);
			for (var t = 16; t < 64; t++) { var s0 = rr(W[t-15],7) ^ rr(W[t-15],18) ^ (W[t-15]>>>3); var s1 = rr(W[t-2],17) ^ rr(W[t-2],19) ^ (W[t-2]>>>10); W[t] = (W[t-16]+s0+W[t-7]+s1)|0; }
			var a=H[0],b=H[1],c=H[2],d=H[3],e=H[4],f=H[5],g=H[6],h=H[7];
			for (var t = 0; t < 64; t++) { var S1=rr(e,6)^rr(e,11)^rr(e,25); var ch=(e&f)^(~e&g); var t1=(h+S1+ch+K[t]+W[t])|0; var S0=rr(a,2)^rr(a,13)^rr(a,22); var maj=(a&b)^(a&c)^(b&c); var t2=(S0+maj)|0; h=g;g=f;f=e;e=(d+t1)|0;d=c;c=b;b=a;a=(t1+t2)|0; }
			H[0]=(H[0]+a)|0;H[1]=(H[1]+b)|0;H[2]=(H[2]+c)|0;H[3]=(H[3]+d)|0;H[4]=(H[4]+e)|0;H[5]=(H[5]+f)|0;H[6]=(H[6]+g)|0;H[7]=(H[7]+h)|0;
		}
		var hex = '';
		for (var i = 0; i < 8; i++) hex += ('00000000'+(H[i]>>>0).toString(16)).slice(-8);
		return hex;
	};
})();

function calcSHA256(input) {
	var display = document.getElementById('sha256-display');
	display.textContent = '计算中...';
	var reader = new FileReader();
	reader.onload = function () {
		display.textContent = 'SHA256: ' + sha256(reader.result);
	};
	reader.readAsArrayBuffer(input.files[0]);
}

function handleSubmit(e) {
	e.preventDefault();
	var form = e.target;
	var btn = form.querySelector('button[type=submit]');
	btn.disabled = true;
	btn.textContent = '上传中...';

	fetch('/', {
		method: 'POST',
		body: new FormData(form)
	}).then(function(resp) {
		if (!resp.ok) return showFail();
		showStep(1, '校验中...');
		pollUpgradeStatus();
	}).catch(function() { showFail(); });
}

var ST=['上传','验证','刷写','重启','访问'];
function showStep(cur, desc) {
	var h='';
	for(var i=0;i<5;i++) h+=(i<cur?ST[i]+'ok ':i===cur?ST[i]+'<svg class="icon icon-spin" viewBox="0 0 24 24"><use href="icons.svg#icon-refresh"/></svg> ':'- '+ST[i]+' ');
	document.querySelector('.card').innerHTML='<h2>'+ST[cur]+'</h2><p>'+h+'</p><p>'+desc+'</p>';
}
function showFail(isTypeMismatch) {
	document.querySelector('.card').innerHTML='<h2>验证失败</h2><div class="error"><p>'+(isTypeMismatch?'类型不匹配':'大小不匹配')+'</p></div><button onclick="window.open(\'term.html\',\'_blank\')">终端详情</button>';
}
function pollUpgradeStatus() {
	var done = 0;
	function check() {
		if (done) return;
		fetch('/upgrade_status').then(function(r) { return r.text(); }).then(function(s) {
			if (s === 'type_mismatch') { done = 1; return showFail(true); }
			if (s === 'rebooting') { done = 1; return pingDevice(); }
			if (s === 'flashing') { showStep(2, '写入中...'); return setTimeout(check, 1000); }
			showStep(1, '校验中...');
			setTimeout(check, 500);
		}).catch(function() { setTimeout(check, 2000); });
	}
	showStep(1, '校验中...');
	setTimeout(check, 500);
}
function pingDevice() {
	var p = window.location.origin.match(/^(https?:\/\/)/)[1];
	var ips = [window.location.origin, p+'192.168.1.1', p+'192.168.0.1', p+'192.168.10.1', p+'192.168.20.1', p+'192.168.30.1', p+'6.6.6.6', p+'6.7.8.9'];
	var ph = [[20,20,'设备重启中...'],[40,100,'系统加载中...'],[20,10,'尝试连接中...']];
	var pi = 0;
	function run() {
		if (pi >= ph.length) return showStep(4, '请手动访问设备ip地址');
		var c = ph[pi++];
		showStep(3, c[2]);
		setTimeout(function() {
			var r = 0;
			function go() {
				if (r++ >= c[1]) return run();
				var hit = 0, fail = 0;
				ips.forEach(function(ip) {
					fetch(ip, {mode:'no-cors', cache:'no-cache'}).then(function() {
						!hit++ && (window.top.location.href = ip);
					}).catch(function() {
						++fail >= ips.length && setTimeout(go, 500);
					});
				});
			}
			go();
		}, c[0] * 1000);
	}
	run();
}

document.querySelector('form').addEventListener('submit', handleSubmit);
document.querySelector('input[type=file]').addEventListener('change', function () {
	calcSHA256(this);
});

(function() {
	function apply(v) {
		v ? document.documentElement.setAttribute('data-theme', v) : document.documentElement.removeAttribute('data-theme');
	}
	window.addEventListener('storage', function(e) {
		if (e.key === 'theme') apply(e.newValue || '');
	});
	window.addEventListener('message', function(e) {
		if (e.data && e.data.type === 'theme') apply(e.data.theme || '');
	});
})();