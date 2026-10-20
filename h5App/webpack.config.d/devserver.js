// 让 webpack dev server 额外把 static_server/static 作为静态目录发布。
// 目的：index.html 可以用相对路径 ./nativevue2.js 加载业务 bundle，
// 电脑（localhost:8080）和手机（<电脑IP>:8080）访问同一个地址即可，无需 whistle 代理。
//
// 注意：本文件会被复制到 build/js/packages/<project>/ 下执行，__dirname 不是工程根目录，
// 因此向上查找 settings.gradle.kts 来定位仓库根目录。
const fs = require('fs')
const path = require('path')

function findRepoRoot(startDir) {
  let dir = startDir
  while (dir !== path.dirname(dir)) {
    if (fs.existsSync(path.join(dir, 'settings.gradle.kts'))) {
      return dir
    }
    dir = path.dirname(dir)
  }
  return process.cwd()
}

const extraStatic = path.join(findRepoRoot(__dirname), 'static_server', 'static')

config.devServer = config.devServer || {}
const current = config.devServer.static
const entries = Array.isArray(current) ? current : current ? [current] : []

config.devServer.static = entries.concat([{ directory: extraStatic, publicPath: '/' }])

// 允许通过非 localhost 的域名访问（内网穿透/ngrok/cloudflared 的域名），
// 否则 webpack-dev-server 默认只放行本机 Host，会返回 Invalid Host header
config.devServer.allowedHosts = 'all'

// 隧道域名下 HMR 的 websocket 需要按当前页面 host 推导，避免写死 localhost:8080
config.devServer.client = Object.assign({}, config.devServer.client, {
  webSocketURL: 'auto://0.0.0.0:0/ws'
})