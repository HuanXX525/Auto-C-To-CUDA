#!/bin/bash

# 设置Git用户配置的函数
set_git_profile() {
    local name="$1"
    local email="$2"
    
    echo -e "\033[32m设置Git用户配置...\033[0m"
    git config --global user.name "$name"
    git config --global user.email "$email"
    echo -e "\033[33m用户名已设置为: $name\033[0m"
    echo -e "\033[33m邮箱已设置为: $email\033[0m"
    echo -e "\033[32mGit配置更新完成!\033[0m"
}

# 检查参数
if [ $# -ne 1 ]; then
    echo -e "\033[31m错误: 请提供一个参数 ('me' 或 'ai')\033[0m"
    echo "用法: $0 {me|ai}"
    exit 1
fi

# 转换为小写
profile=$(echo "$1" | tr '[:upper:]' '[:lower:]')

# 根据参数设置配置
case "$profile" in
    "me")
        set_git_profile "Huanxx" "shy2757539057@163.com"
        # echo -e "\033[36m注意: 请修改脚本中的姓名和邮箱为您自己的信息\033[0m"
        ;;
    "ai")
        set_git_profile "Little-Helper-Aria" "qq2757539057@gmail.com"
        # echo -e "\033[36m注意: 请修改脚本中的AI配置姓名和邮箱\033[0m"
        ;;
    *)
        echo -e "\033[31m无效的参数。请使用 'me' 或 'ai'\033[0m"
        exit 1
        ;;
esac

# 显示当前配置
echo -e "\n\033[32m当前Git配置:\033[0m"
git config --global user.name
git config --global user.email