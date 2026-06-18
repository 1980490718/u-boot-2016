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
	document.querySelector('.card').innerHTML = '<h2>更新中</h2><p>正在擦写对应分区</p><div class="spinner"></div>';
}

function showRebooting() {
	document.querySelector('.card').innerHTML = '<h2>更新完成</h2><p>正在重启</p>';
}

function showFail() {
	document.querySelector('.card').innerHTML = '<h2>失败</h2><div class="error"><p>查看终端信息排查</p></div><button onclick="window.open(\'term.html\',\'_blank\')">失败详情</button>';
}

function pollUpgradeStatus() {
	var phase = 'verifying';
	function check() {
		fetch('/upgrade_status').then(function(resp) {
			return resp.text();
		}).then(function(status) {
			switch (status) {
				case 'failed':
					showFail();
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
					setTimeout(check, 500);
			}
		}).catch(function() {
			setTimeout(check, 3000);
		});
	}
	setTimeout(check, 1000);
}