<script setup>
import { ref } from 'vue'

const props = defineProps({
    selectedCount: Number,
    currentParentId: Number
})

const emit = defineEmits(['createFolder', 'upload', 'delete', 'loadStorage', 'openRecycle', 'openSearch'])

const showNewFolderDialog = ref(false)
const newFolderName = ref('')

function handleCreateFolder() {
    if (newFolderName.value.trim()) {
        emit('createFolder', newFolderName.value.trim())
        newFolderName.value = ''
        showNewFolderDialog.value = false
    }
}

function handleImportUpload(e) {
    const input = document.createElement('input')
    input.type = 'file'
    input.multiple = true
    input.webkitdirectory = false
    input.onchange = (e) => {
        const files = e.target.files
        if (files.length > 0) {
            emit('upload', Array.from(files))
        }
    }
    input.click()
}

function handleImportFolder() {
    const input = document.createElement('input')
    input.type = 'file'
    input.webkitdirectory = true
    input.onchange = (e) => {
        const files = e.target.files
        if (files.length > 0) {
            emit('upload', Array.from(files))
        }
    }
    input.click()
}
</script>

<template>
    <div class="toolbar">
        <div class="toolbar-left">
            <div class="logo">
                <span class="logo-icon">☁️</span>
                <span class="logo-text">我的网盘</span>
            </div>
        </div>

        <div class="toolbar-center">
            <button class="btn btn-primary" @click="handleImportUpload">
                📤 上传文件
            </button>
            <button class="btn btn-primary" @click="handleImportFolder">
                📁 上传文件夹
            </button>
            <button class="btn btn-secondary" @click="showNewFolderDialog = true">
                📂 新建文件夹
            </button>
            <button
                class="btn btn-danger"
                :disabled="selectedCount === 0"
                @click="emit('delete')"
            >
                🗑️ 删除{{ selectedCount > 0 ? `(${selectedCount})` : '' }}
            </button>
            <button class="btn btn-secondary" @click="emit('openSearch')">
                🔍 搜索
            </button>
        </div>

        <div class="toolbar-right">
            <button class="btn btn-secondary btn-sm" @click="emit('openRecycle')">
                🗑️ 回收站
            </button>
        </div>

        <!-- 新建文件夹对话框 -->
        <div v-if="showNewFolderDialog" class="modal-overlay" @click.self="showNewFolderDialog = false">
            <div class="modal">
                <h3>新建文件夹</h3>
                <input
                    v-model="newFolderName"
                    class="input"
                    style="width: 100%;"
                    placeholder="请输入文件夹名称"
                    @keyup.enter="handleCreateFolder"
                    autofocus
                />
                <div class="modal-actions">
                    <button class="btn btn-secondary" @click="showNewFolderDialog = false">取消</button>
                    <button class="btn btn-primary" @click="handleCreateFolder">创建</button>
                </div>
            </div>
        </div>
    </div>
</template>

<style scoped>
.toolbar {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 10px 20px;
    background: white;
    border-bottom: 1px solid #e0e0e0;
    gap: 12px;
}

.toolbar-left, .toolbar-center, .toolbar-right {
    display: flex;
    align-items: center;
    gap: 8px;
}

.toolbar-center {
    flex: 1;
    justify-content: center;
}

.logo {
    display: flex;
    align-items: center;
    gap: 8px;
    font-weight: bold;
    font-size: 18px;
}

.logo-icon {
    font-size: 24px;
}

.logo-text {
    color: #1976d2;
}

.btn:disabled {
    opacity: 0.5;
    cursor: not-allowed;
}
</style>
