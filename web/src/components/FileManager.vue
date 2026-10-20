<script setup>
import { ref } from 'vue'

const props = defineProps({
    files: Array,
    loading: Boolean,
    selectedFiles: Array,
    parentId: Number
})

const emit = defineEmits(['navigate', 'select', 'selectAll', 'download', 'rename', 'move', 'copy', 'upload'])

const dragOver = ref(false)
const contextMenu = ref(null)
const renamingId = ref(null)
const renameValue = ref('')

function isSelected(file) {
    return props.selectedFiles.some(f => f.id === file.id)
}

function getFileIcon(file) {
    if (file.type === 'dir') return '📁'
    const ext = file.name.split('.').pop()?.toLowerCase()
    const icons = {
        pdf: '📄', doc: '📝', docx: '📝',
        xls: '📊', xlsx: '📊', ppt: '📽️', pptx: '📽️',
        jpg: '🖼️', jpeg: '🖼️', png: '🖼️', gif: '🖼️', bmp: '🖼️', webp: '🖼️',
        mp4: '🎬', avi: '🎬', mkv: '🎬', mov: '🎬', wmv: '🎬',
        mp3: '🎵', wav: '🎵', flac: '🎵', aac: '🎵',
        zip: '📦', rar: '📦', '7z': '📦', tar: '📦', gz: '📦',
        exe: '⚙️', msi: '⚙️',
        txt: '📃', md: '📃', json: '📃', xml: '📃'
    }
    return icons[ext] || '📎'
}

function formatSize(bytes) {
    if (bytes === 0) return '-'
    const units = ['B', 'KB', 'MB', 'GB', 'TB']
    let size = bytes
    let i = 0
    while (size >= 1024 && i < units.length - 1) {
        size /= 1024
        i++
    }
    return size.toFixed(1) + ' ' + units[i]
}

function formatTime(ts) {
    if (!ts) return '-'
    const d = new Date(ts * 1000)
    return d.toLocaleString('zh-CN')
}

function handleDrop(e) {
    dragOver.value = false
    const files = e.dataTransfer.files
    if (files.length > 0) {
        emit('upload', Array.from(files))
    }
}

function handleDragOver(e) {
    e.preventDefault()
    dragOver.value = true
}

function handleDragLeave() {
    dragOver.value = false
}

function showContextMenu(e, file) {
    e.preventDefault()
    contextMenu.value = {
        x: e.clientX,
        y: e.clientY,
        file: file
    }
}

function startRename(file) {
    renamingId.value = file.id
    renameValue.value = file.name
    contextMenu.value = null
}

function confirmRename(file) {
    if (renameValue.value.trim() && renameValue.value !== file.name) {
        emit('rename', file.id, renameValue.value.trim())
    }
    renamingId.value = null
}

function closeContextMenu() {
    contextMenu.value = null
}
</script>

<template>
    <div
        class="file-manager"
        :class="{ 'drag-over': dragOver }"
        @drop="handleDrop"
        @dragover="handleDragOver"
        @dragleave="handleDragLeave"
        @click="closeContextMenu"
    >
        <!-- 表头 -->
        <div class="file-header">
            <div class="col-check">
                <input type="checkbox" :checked="selectedFiles.length === files.length && files.length > 0"
                       @change="emit('selectAll')" @click.stop>
            </div>
            <div class="col-name">名称</div>
            <div class="col-size">大小</div>
            <div class="col-time">创建时间</div>
            <div class="col-actions">操作</div>
        </div>

        <!-- 文件列表 -->
        <div class="file-list" v-if="!loading">
            <div
                v-for="file in files"
                :key="file.id"
                class="file-row"
                :class="{ selected: isSelected(file) }"
                @click.stop="emit('select', file)"
                @dblclick="emit('navigate', file)"
                @contextmenu="showContextMenu($event, file)"
            >
                <div class="col-check">
                    <input type="checkbox" :checked="isSelected(file)"
                           @change="emit('select', file)" @click.stop>
                </div>
                <div class="col-name">
                    <span class="file-icon">{{ getFileIcon(file) }}</span>
                    <template v-if="renamingId === file.id">
                        <input v-model="renameValue" class="input rename-input"
                               @keyup.enter="confirmRename(file)"
                               @keyup.escape="renamingId = null"
                               @click.stop autofocus>
                    </template>
                    <template v-else>
                        <span class="file-name" :title="file.name">{{ file.name }}</span>
                    </template>
                </div>
                <div class="col-size">{{ formatSize(file.size) }}</div>
                <div class="col-time">{{ formatTime(file.created_at) }}</div>
                <div class="col-actions">
                    <button class="icon-btn" title="下载" @click.stop="emit('download', file)" v-if="file.type === 'file'">
                        ⬇️
                    </button>
                    <button class="icon-btn" title="重命名" @click.stop="startRename(file)">
                        ✏️
                    </button>
                    <button class="icon-btn danger" title="删除" @click.stop="$emit('select', file)">
                        🗑️
                    </button>
                </div>
            </div>

            <!-- 空状态 -->
            <div v-if="files.length === 0" class="empty-state">
                <div class="empty-icon">📂</div>
                <p>此目录为空</p>
                <p class="hint">拖拽文件到此处上传</p>
            </div>
        </div>

        <!-- 加载状态 -->
        <div v-if="loading" class="loading-state">
            <div class="spinner"></div>
            <p>加载中...</p>
        </div>

        <!-- 拖拽提示 -->
        <div v-if="dragOver" class="drop-overlay">
            <div class="drop-message">
                <span class="drop-icon">📥</span>
                <p>释放文件以上传</p>
            </div>
        </div>

        <!-- 右键菜单 -->
        <div v-if="contextMenu" class="context-menu" :style="{ left: contextMenu.x + 'px', top: contextMenu.y + 'px' }">
            <div class="menu-item" @click.stop="emit('download', contextMenu.file)" v-if="contextMenu.file.type === 'file'">
                ⬇️ 下载
            </div>
            <div class="menu-item" @click.stop="startRename(contextMenu.file)">
                ✏️ 重命名
            </div>
            <div class="menu-item" @click.stop="emit('copy', contextMenu.file.id, 0)">
                📋 复制
            </div>
            <div class="menu-item" @click.stop="emit('move', contextMenu.file.id, 0)">
                ✂️ 移动
            </div>
            <div class="menu-divider"></div>
            <div class="menu-item danger" @click.stop="emit('select', contextMenu.file); $emit('delete')">
                🗑️ 删除
            </div>
        </div>
    </div>
</template>

<style scoped>
.file-manager {
    flex: 1;
    overflow-y: auto;
    position: relative;
    padding: 0 20px;
}

.file-header {
    display: flex;
    align-items: center;
    padding: 10px 0;
    border-bottom: 2px solid #e0e0e0;
    font-weight: 600;
    font-size: 13px;
    color: #666;
    position: sticky;
    top: 0;
    background: #f5f6fa;
    z-index: 10;
}

.file-row {
    display: flex;
    align-items: center;
    padding: 8px 0;
    border-bottom: 1px solid #eee;
    cursor: pointer;
    transition: background 0.1s;
}

.file-row:hover {
    background: #f0f4f8;
}

.file-row.selected {
    background: #e3f2fd;
}

.col-check { width: 40px; display: flex; align-items: center; }
.col-name { flex: 1; display: flex; align-items: center; gap: 8px; min-width: 0; }
.col-size { width: 100px; text-align: right; font-size: 13px; color: #888; }
.col-time { width: 180px; font-size: 13px; color: #888; }
.col-actions { width: 120px; display: flex; gap: 2px; opacity: 0; transition: opacity 0.2s; }
.file-row:hover .col-actions { opacity: 1; }

.file-name {
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    font-size: 14px;
}

.rename-input {
    width: 200px;
    padding: 2px 6px;
    font-size: 13px;
}

.empty-state {
    text-align: center;
    padding: 80px 20px;
    color: #999;
}

.empty-icon {
    font-size: 64px;
    margin-bottom: 16px;
}

.hint {
    font-size: 13px;
    margin-top: 8px;
}

.loading-state {
    text-align: center;
    padding: 60px;
    color: #999;
}

.spinner {
    width: 32px;
    height: 32px;
    border: 3px solid #e0e0e0;
    border-top-color: #1976d2;
    border-radius: 50%;
    animation: spin 0.8s linear infinite;
    margin: 0 auto 12px;
}

@keyframes spin {
    to { transform: rotate(360deg); }
}

.drop-overlay {
    position: absolute;
    top: 0; left: 0; right: 0; bottom: 0;
    background: rgba(25, 118, 210, 0.1);
    border: 3px dashed #1976d2;
    display: flex;
    align-items: center;
    justify-content: center;
    z-index: 100;
}

.drop-message {
    text-align: center;
    color: #1976d2;
}

.drop-icon {
    font-size: 48px;
}

.context-menu {
    position: fixed;
    background: white;
    border-radius: 8px;
    box-shadow: 0 4px 16px rgba(0,0,0,0.15);
    padding: 4px 0;
    min-width: 150px;
    z-index: 1000;
}

.menu-item {
    padding: 8px 16px;
    cursor: pointer;
    font-size: 14px;
    transition: background 0.1s;
}

.menu-item:hover {
    background: #f5f5f5;
}

.menu-item.danger {
    color: #d32f2f;
}

.menu-divider {
    height: 1px;
    background: #eee;
    margin: 4px 0;
}
</style>
