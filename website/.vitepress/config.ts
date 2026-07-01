import { defineConfig } from 'vitepress'

// SoFixer 官网配置。
// 设计：扁平、深色、青蓝强调色（#39d0d8），无紫色、无大圆角。
// 自定义样式见 ./theme/ 下。
export default defineConfig({
  lang: 'zh-CN',
  title: 'SoFixer',
  description: '修复从内存 dump 的 Android SO 文件 · 面向 AI Agent 的结构化 CLI',

  // 站点根：发布到 https://android-security-engineer.github.io/SoFixer-skills/ 时需要带 base 路径。
  // 若部署到自定义域名根路径，改为 '/' 即可。
  base: '/SoFixer-skills/',

  cleanUrls: true,
  lastUpdated: true,

  head: [
    ['meta', { name: 'theme-color', content: '#0d1117' }],
    ['meta', { name: 'apple-mobile-web-app-capable', content: 'yes' }]
  ],

  themeConfig: {
    // 站点头部 logo / 标题
    siteTitle: 'SoFixer',
    logo: '/logo.svg',

    // 顶部导航
    nav: [
      { text: '指南', items: [
        { text: '它解决什么问题', link: '/guide/problem' },
        { text: '前置背景知识', link: '/guide/background' },
        { text: '工作原理', link: '/guide/how-it-works' },
        { text: '快速开始', link: '/guide/getting-started' }
      ]},
      { text: 'CLI', items: [
        { text: '命令总览', link: '/cli/' },
        { text: 'fix 修复', link: '/cli/fix' },
        { text: 'info 查看', link: '/cli/info' },
        { text: 'verify 校验', link: '/cli/verify' }
      ]},
      { text: 'AI Agent 接入', link: '/ai-agent/' },
      { text: 'GitHub', link: 'https://github.com/android-security-engineer/SoFixer-skills' }
    ],

    // 侧边栏：按顶级目录分组
    sidebar: {
      '/guide/': [
        {
          text: '认识 SoFixer',
          items: [
            { text: '它解决什么问题', link: '/guide/problem' },
            { text: '前置背景知识', link: '/guide/background' },
            { text: '工作原理', link: '/guide/how-it-works' },
            { text: '快速开始', link: '/guide/getting-started' }
          ]
        },
        {
          text: '深入原理',
          items: [
            { text: '磁盘 vs 内存', link: '/guide/disk-vs-memory' },
            { text: '重定位原理', link: '/guide/relocation' },
            { text: 'ELF 字段速查', link: '/guide/elf-reference' }
          ]
        }
      ],
      '/cli/': [
        {
          text: 'CLI 参考',
          items: [
            { text: '命令总览', link: '/cli/' },
            { text: 'fix — 修复', link: '/cli/fix' },
            { text: 'info — 查看', link: '/cli/info' },
            { text: 'verify — 校验', link: '/cli/verify' },
            { text: '输出格式与错误码', link: '/cli/output' }
          ]
        }
      ],
      '/ai-agent/': [
        {
          text: 'AI Agent 接入',
          items: [
            { text: '概览', link: '/ai-agent/' },
            { text: 'Claude Code', link: '/ai-agent/claude-code' },
            { text: 'Codex', link: '/ai-agent/codex' }
          ]
        }
      ]
    },

    // 社交链接
    socialLinks: [
      { icon: 'github', link: 'https://github.com/android-security-engineer/SoFixer-skills' }
    ],

    // 搜索
    search: {
      provider: 'local'
    },

    // 页脚
    footer: {
      message: '基于 VitePress 构建',
      copyright: 'SoFixer · author F8LEFT (currwin) · MIT License'
    },

    outline: {
      label: '本页内容',
      level: [2, 3]
    },

    docFooter: {
      prev: '上一页',
      next: '下一页'
    },

    lastUpdatedText: '最后更新',

    returnToTopLabel: '回到顶部',
    sidebarMenuLabel: '目录',
    darkModeSwitchLabel: '主题',
    lightModeSwitchTitle: '切换到浅色主题',
    darkModeSwitchTitle: '切换到深色主题'
  }
})
