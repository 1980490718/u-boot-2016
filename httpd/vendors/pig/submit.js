function handleSubmit(e) {
	e.preventDefault();
	var form = e.target;
	var btn = form.querySelector('button[type=submit]');
	btn.disabled = true;
	btn.textContent = '正在上传';

	fetch('/', {
		method: 'POST',
		body: new FormData(form)
	}).then(function(resp) {
		if (resp.ok) {
			showVerifying();
			pollUpgradeStatus();
		} else {
			showFail();
		}
	}).catch(function() {
		showFail();
	});

	return false;
}

function showVerifying() {
	document.querySelector('.card').innerHTML = '<h2>验证中</h2><p>验证文件类型及大小</p><div class="spinner"></div>';
}

function showFlashing() {
	document.querySelector('.card').innerHTML = '<h2>更新中</h2><p>正在处理更新</p><div class="spinner"></div>';
}

function showRebooting() {
	document.querySelector('.card').innerHTML = '<h2>更新完成</h2><p>正在重启</p>';
}

function showFail(isTypeMismatch) {
	document.querySelector('.card').innerHTML = '<h2>验证失败</h2><div class="error"><p>' + (isTypeMismatch ? '类型不匹配' : '大小不匹配') + '</p></div><button onclick="window.open(\'term.html\',\'_blank\')">终端详情</button>';
}

function pollUpgradeStatus() {
	var phase = 'verifying';
	function check() {
		fetch('/upgrade_status').then(function(resp) {
			return resp.text();
		}).then(function(status) {
			switch (status) {
				case 'type_mismatch':
					showFail(true);
					break;
				case 'rebooting':
					if (phase !== 'rebooting') {
						phase = 'rebooting';
						showRebooting();
					}
					setTimeout(check, 3000);
					break;
				case 'flashing':
					if (phase !== 'flashing') {
						phase = 'flashing';
						showFlashing();
					}
					setTimeout(check, 3000);
					break;
				default:
					setTimeout(check, 100);
			}
		}).catch(function() {
			setTimeout(check, 3000);
		});
	}
	setTimeout(check, 1000);
}