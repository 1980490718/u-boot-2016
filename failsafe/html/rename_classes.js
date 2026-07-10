#!/usr/bin/env node
'use strict';

var fs = require('fs');
var path = require('path');

var inputDir = process.argv[2];
var outputDir = process.argv[3];

if (!inputDir || !outputDir) {
	console.error('Usage: node rename_classes.js <input_dir> <output_dir>');
	process.exit(1);
}

fs.mkdirSync(outputDir, { recursive: true });

function escapeRegex(s) {
	return s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
}

var idx = 0;
function shortName() {
	var chars = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ';
	var i = idx++;
	var name = '';
	do {
		name = chars[i % chars.length] + name;
		i = Math.floor(i / chars.length) - 1;
	} while (i >= 0);
	return name;
}

var allClasses = {};
var files = fs.readdirSync(inputDir).filter(function(f) {
	return fs.statSync(path.join(inputDir, f)).isFile();
});

for (var fi = 0; fi < files.length; fi++) {
	var content = fs.readFileSync(path.join(inputDir, files[fi]), 'utf8');
	var ext = path.extname(files[fi]);
	var m;

	if (ext === '.css') {
		var re = /\.([a-zA-Z_][a-zA-Z0-9_-]*)/g;
		var urlRe = /url\([^)]*\)/g;
		var cssNoUrl = content.replace(urlRe, '');
		while ((m = re.exec(cssNoUrl)) !== null) {
			allClasses[m[1]] = (allClasses[m[1]] || 0) + 1;
		}
	} else if (ext === '.html' || ext === '.htm') {
		var re = /class="([^"]+)"/g;
		while ((m = re.exec(content)) !== null) {
			m[1].split(/\s+/).forEach(function(c) {
				if (c) allClasses[c] = (allClasses[c] || 0) + 1;
			});
		}
	}
}

var existingShort = {};
Object.keys(allClasses).forEach(function(name) {
	if (name.length <= 3) {
		existingShort[name] = true;
	}
});

var classMapping = {};
Object.keys(allClasses).sort().forEach(function(name) {
	if (name.length > 3) {
		var candidate;
		do {
			candidate = shortName();
		} while (existingShort[candidate]);
		classMapping[name] = candidate;
		existingShort[candidate] = true;
	}
});

function replaceClassInJs(code) {
	Object.keys(classMapping).forEach(function(long) {
		var short = classMapping[long];
		var e = escapeRegex(long);
		code = code.replace(
			new RegExp("classList\\.(add|remove|toggle|contains)\\(([\\s\\S]*?)\\)", 'g'),
			function(m, method, args) {
				var replaced = args.replace(new RegExp("([\"'])" + e + "\\1", 'g'), '$1' + short + '$1');
				return 'classList.' + method + '(' + replaced + ')';
			}
		);
		code = code.replace(
			new RegExp("querySelector(All)?\\(([\"'])([\\s\\S]*?)\\2\\)", 'g'),
			function(m, method, q, selector) {
				var count = 0;
				var replaced = selector.replace(new RegExp('\\.' + e + '(?=[^a-zA-Z0-9_-]|$)', 'g'), function() {
					if (count < 5) { count++; return '.' + short; }
					return '.' + e;
				});
				return 'querySelector' + (method || '') + '(' + q + replaced + q + ')';
			}
		);
		code = code.replace(
			new RegExp("className\\s*(?<![!=<>])=(?!=)\\s*([^;\\n]+)", 'g'),
			function(m, expr) {
				var replaced = expr.replace(/(["'])([^"']*?)\1/g, function(qm, q, str) {
					return q + str.split(/\s+/).map(function(c) {
						return c === long ? short : c;
					}).join(' ') + q;
				});
				return 'className=' + replaced;
			}
		);
		code = code.replace(
			new RegExp("className\\s*\\+=\\s*([^;\\n]+)", 'g'),
			function(m, expr) {
				var replaced = expr.replace(/(["'])([^"']*?)\1/g, function(qm, q, str) {
					return q + str.split(/\s+/).map(function(c) {
						return c === long ? short : c;
					}).join(' ') + q;
				});
				return 'className+=' + replaced;
			}
		);
		code = code.replace(
			new RegExp("setAttribute\\(([\"'])class\\1\\s*,\\s*([\"'])([\\s\\S]*?)\\2\\)", 'g'),
			function(m, q1, q2, classes) {
				return 'setAttribute(' + q1 + 'class' + q1 + ',' + q2 + classes.split(/\s+/).map(function(c) {
					return c === long ? short : c;
				}).join(' ') + q2 + ')';
			}
		);
		code = code.replace(
			new RegExp("\\.matches\\(([\"'])([\\s\\S]*?)\\1\\)", 'g'),
			function(m, q, selector) {
				var count = 0;
				var replaced = selector.replace(new RegExp('\\.' + e + '(?=[^a-zA-Z0-9_-]|$)', 'g'), function() {
					if (count < 5) { count++; return '.' + short; }
					return '.' + e;
				});
				return '.matches(' + q + replaced + q + ')';
			}
		);
		code = code.replace(
			new RegExp("\\.closest\\(([\"'])([\\s\\S]*?)\\1\\)", 'g'),
			function(m, q, selector) {
				var count = 0;
				var replaced = selector.replace(new RegExp('\\.' + e + '(?=[^a-zA-Z0-9_-]|$)', 'g'), function() {
					if (count < 5) { count++; return '.' + short; }
					return '.' + e;
				});
				return '.closest(' + q + replaced + q + ')';
			}
		);
		code = code.replace(
			new RegExp("getElementsByClassName\\(([\"'])([\\s\\S]*?)\\1\\)", 'g'),
			function(m, q, classes) {
				return 'getElementsByClassName(' + q + classes.split(/\s+/).map(function(c) {
					return c === long ? short : c;
				}).join(' ') + q + ')';
			}
		);
		code = code.replace(
			new RegExp('class="([^"]*\\b' + e + '\\b[^"]*)"', 'g'),
			function(m, classes) {
				return 'class="' + classes.split(/\s+/).map(function(c) {
					return c === long ? short : c;
				}).join(' ') + '"';
			}
		);
	});
	return code;
}

for (var fi = 0; fi < files.length; fi++) {
	var inputPath = path.join(inputDir, files[fi]);
	var outputPath = path.join(outputDir, files[fi]);
	var content = fs.readFileSync(inputPath, 'utf8');
	var ext = path.extname(files[fi]);

	if (ext === '.css') {
		Object.keys(classMapping).forEach(function(long) {
			var short = classMapping[long];
			content = content.replace(
				new RegExp('\\.' + escapeRegex(long) + '(?=[^a-zA-Z0-9_-]|$)', 'g'),
				'.' + short
			);
		});
	} else if (ext === '.html' || ext === '.htm') {
		content = content.replace(/class="([^"]+)"/g, function(match, classes) {
			return 'class="' + classes.split(/\s+/).map(function(c) {
				return classMapping[c] || c;
			}).join(' ') + '"';
		});
		content = content.replace(/<script([^>]*)>([\s\S]*?)<\/script>/gi, function(match, attrs, scriptContent) {
			if (/\bsrc\s*=/.test(attrs)) {
				return match;
			}
			return '<script' + attrs + '>' + replaceClassInJs(scriptContent) + '</script>';
		});
	} else if (ext === '.js') {
		content = replaceClassInJs(content);
	}

	fs.writeFileSync(outputPath, content);
}

var classEntries = Object.keys(classMapping);
var orig = 0, shrt = 0;

if (classEntries.length > 0) {
	console.error('Class rename mapping (' + classEntries.length + ' classes):');
	classEntries.forEach(function(k) {
		console.error('  .' + k + ' -> .' + classMapping[k]);
		orig += k.length;
		shrt += classMapping[k].length;
	});
	console.error('Total: ' + orig + ' -> ' + shrt + ' chars (saved ' + (orig - shrt) + ')');
} else {
	console.error('No classes to rename');
}