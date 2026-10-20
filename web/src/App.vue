<script setup>
import { ref, computed, onMounted, watch } from 'vue'
import FileManager from './components/FileManager.vue'
import Toolbar from './components/Toolbar.vue'
import StatusBar from './components/StatusBar.vue'
import UploadPanel from './components/UploadPanel.vue'
import RecycleBin from './components/RecycleBin.vue'
import SearchPanel from './components/SearchPanel.vue'
import * as api from './api.js'

// 状态
const currentParentId = ref(0)
const currentPath = ref([{ id: 0, name: '根目录' }])
const files = ref([])
const loading = ref(false)
const sortBy = ref('name')
const sortOrder = ref('asc')
const selectedFiles = ref([])
const storageInfo = ref(null)
const showUploadPanel = ref(false)
const showRecycleBin = ref(false)
const showSearchPanel = ref(false)
const uploadQueue = ref([])
const notification = ref(null)

// 加载文件列表
async function loadFiles() {
    loading.value = true
    try {
        files.value = await api.listFiles(currentParentId.value, sortBy.value, sortOrder.value)
    } catch (e) {
        showNotification('加载文件列表失败: ' + e.message, 'error')
    }
    loading.value = false
}

// 导航到文件夹
function navigateTo(item) {
    if (item.type === 'dir') {
        currentParentId.value = item.id
        // 更新路径
        const idx = currentPath.value.findIndex(p => p.id === item.id)
        if (idx >= 0) {
            currentPath.value = currentPath.value.slice(0, idx + 1)
        } else {
            currentPath.value.push({ id: item.id, name: item.name })
        }
        selectedFiles.value = []
        loadFiles()
    }
}

// 导航到路径
function navigateToPath(idx) {
    if (idx === currentPath.value.length - 1) return
    currentParentId.value = currentPath.value[idx].id
    currentPath.value = currentPath.value.slice(0, idx + 1)
    selectedFiles.value = []
    loadFiles()
}

// 排序切换
function toggleSort(field) {
    if (sortBy.value === field) {
        sortOrder.value = sortOrder.value === 'asc' ? 'desc' : 'asc'
    } else {
        sortBy.value = field
        sortOrder.value = 'asc'
    }
    loadFiles()
}

// 选择文件
function toggleSelect(file) {
    const idx = selectedFiles.value.findIndex(f => f.id === file.id)
    if (idx >= 0) {
        selectedFiles.value.splice(idx, 1)
    } else {
        selectedFiles.value.push(file)
    }
}

function selectAll() {
    if (selectedFiles.value.length === files.value.length) {
        selectedFiles.value = []
    } else {
        selectedFiles.value = [...files.value]
    }
}

// 创建文件夹
async function createFolder(name) {
    try {
        await api.createFolder(currentParentId.value, name)
        showNotification('文件夹创建成功', 'success')
        loadFiles()
    } catch (e) {
        showNotification('创建失败: ' + e.message, 'error')
    }
}

// 删除选中文件
async function deleteSelected() {
    if (selectedFiles.value.length === 0) return
    if (!confirm(`确定删除选中的 ${selectedFiles.value.length} 个项目？`)) return

    for (const file of selectedFiles.value) {
        try {
            await api.deleteFile(file.id)
        } catch (e) {
            showNotification(`删除 ${file.name} 失败`, 'error')
        }
    }
    showNotification('已移入回收站', 'success')
    selectedFiles.value = []
    loadFiles()
}

// 批量上传
async function handleFilesUpload(fileList) {
    for (const file of fileList) {
        const task = {
            id: Date.now() + Math.random(),
            name: file.name,
            size: file.size,
            progress: 0,
            status: 'uploading'
        }
        uploadQueue.value.push(task)

        try {
            await api.uploadFile(currentParentId.value, file, (progress) => {
                task.progress = progress
            })
            task.status = 'done'
        } catch (e) {
            task.status = 'error'
            task.error = e.message
        }
    }
    loadFiles()
    // 3秒后关闭上传面板
    setTimeout(() => {
        showUploadPanel.value = false
        uploadQueue.value = []
    }, 3000)
}

// 下载文件
function downloadFile(file) {
    const url = api.getDownloadUrl(file.id)
    const a = document.createElement('a')
    a.href = url
    a.download = file.name
    document.body.appendChild(a)
    a.click()
    document.body.removeChild(a)
}

// 重命名
async function renameFile(id, newName) {
    try {
        await api.rename(id, newName)
        showNotification('重命名成功', 'success')
        loadFiles()
    } catch (e) {
        showNotification('重命名失败', 'error')
    }
}

// 移动文件
async function moveFiles(targetParentId) {
    for (const file of selectedFiles.value) {
        try {
            await api.moveFile(file.id, targetParentId)
        } catch (e) {
            showNotification(`移动 ${file.name} 失败`, 'error')
        }
    }
    showNotification('移动成功', 'success')
    selectedFiles.value = []
    loadFiles()
}

// 复制文件
async function copyFiles(targetParentId) {
    for (const file of selectedFiles.value) {
        try {
            await api.copyFile(file.id, targetParentId)
        } catch (e) {
            showNotification(`复制 ${file.name} 失败`, 'error')
        }
    }
    showNotification('复制成功', 'success')
    selectedFiles.value = []
    loadFiles()
}

// 加载存储信息
async function loadStorageInfo() {
    try {
        storageInfo.value = await api.getStorageInfo()
    } catch (e) {}
}

// 通知
function showNotification(msg, type = 'info') {
    notification.value = { message: msg, type }
    setTimeout(() => { notification.value = null }, 3000)
}

onMounted(() => {
    loadFiles()
    loadStorageInfo()
})
</script>

<template>
    <div class="app-container">
        <!-- 工具栏 -->
        <Toolbar
            :selected-count="selectedFiles.length"
            :current-parent-id="currentParentId"
            @create-folder="createFolder"
            @upload="showUploadPanel = true"
            @delete="deleteSelected"
            @load-storage="loadStorageInfo"
            @open-recycle="showRecycleBin = true"
            @open-search="showSearchPanel = true"
        />

        <!-- 主内容 -->
        <div class="main-content">
            <!-- 面包屑导航 -->
            <div class="breadcrumb">
                <span
                    v-for="(item, idx) in currentPath"
                    :key="item.id"
                    class="breadcrumb-item"
                    :class="{ active: idx === currentPath.length - 1 }"
                    @click="navigateToPath(idx)"
                >
                    {{ item.name }}
                    <span v-if="idx < currentPath.length - 1" class="separator">/</span>
                </span>
            </div>

            <!-- 排序栏 -->
            <div class="sort-bar">
                <button class="sort-btn" :class="{ active: sortBy === 'name' }" @click="toggleSort('name')">
                    名称 {{ sortBy === 'name' ? (sortOrder === 'asc' ? '↑' : '↓') : '' }}
                </button>
                <button class="sort-btn" :class="{ active: sortBy === 'size' }" @click="toggleSort('size')">
                    大小 {{ sortBy === 'size' ? (sortOrder === 'asc' ? '↑' : '↓') : '' }}
                </button>
                <button class="sort-btn" :class="{ active: sortBy === 'created' }" @click="toggleSort('created')">
                    创建时间 {{ sortBy === 'created' ? (sortOrder === 'asc' ? '↑' : '↓') : '' }}
                </button>
            </div>

            <!-- 文件管理器 -->
            <FileManager
                :files="files"
                :loading="loading"
                :selected-files="selectedFiles"
                :parent-id="currentParentId"
                @navigate="navigateTo"
                @select="toggleSelect"
                @select-all="selectAll"
                @download="downloadFile"
                @rename="renameFile"
                @move="moveFiles"
                @copy="copyFiles"
                @upload="handleFilesUpload"
            />
        </div>

        <!-- 状态栏 -->
        <StatusBar
            :file-count="files.length"
            :selected-count="selectedFiles.length"
            :storage="storageInfo"
        />

        <!-- 上传面板 -->
        <UploadPanel
            v-if="showUploadPanel"
            :queue="uploadQueue"
            @upload="handleFilesUpload"
            @close="showUploadPanel = false"
        />

        <!-- 回收站 -->
        <RecycleBin
            v-if="showRecycleBin"
            @close="showRecycleBin = false"
            @refresh="loadFiles"
        />

        <!-- 搜索面板 -->
        <SearchPanel
            v-if="showSearchPanel"
            @close="showSearchPanel = false"
            @download="downloadFile"
        />

        <!-- 通知 -->
        <Transition name="fade">
            <div v-if="notification" class="notification" :class="notification.type">
                {{ notification.message }}
            </div>
        </Transition>
    </div>
</template>

<style scoped>
.app-container {
    display: flex;
    flex-direction: column;
    height: 100vh;
    background: #f5f6fa;
}

.main-content {
    flex: 1;
    display: flex;
    flex-direction: column;
    overflow: hidden;
}

.breadcrumb {
    padding: 10px 20px;
    font-size: 14px;
    background: white;
    border-bottom: 1px solid #e0e0e0;
}

.breadcrumb-item {
    cursor: pointer;
    color: #1976d2;
}

.breadcrumb-item.active {
    color: #333;
    cursor: default;
}

.breadcrumb-item .separator {
    margin: 0 6px;
    color: #999;
}

.sort-bar {
    display: flex;
    gap: 8px;
    padding: 8px 20px;
    background: white;
    border-bottom: 1px solid #e0e0e0;
}

.sort-btn {
    background: none;
    border: 1px solid transparent;
    padding: 4px 12px;
    border-radius: 4px;
    cursor: pointer;
    font-size: 13px;
    color: #666;
}

.sort-btn.active {
    background: #e3f2fd;
    color: #1976d2;
    border-color: #bbdefb;
}

.notification {
    position: fixed;
    top: 20px;
    right: 20px;
    padding: 12px 20px;
    border-radius: 8px;
    color: white;
    font-size: 14px;
    z-index: 2000;
    box-shadow: 0 4px 12px rgba(0,0,0,0.15);
}

.notification.success { background: #2e7d32; }
.notification.error { background: #d32f2f; }
.notification.info { background: #1976d2; }

.fade-enter-active, .fade-leave-active {
    transition: opacity 0.3s, transform 0.3s;
}
.fade-enter-from, .fade-leave-to {
    opacity: 0;
    transform: translateY(-10px);
}
</style>
