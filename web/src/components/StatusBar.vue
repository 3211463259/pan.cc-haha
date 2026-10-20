<script setup>
defineProps({
    fileCount: Number,
    selectedCount: Number,
    storage: Object
})

function formatSize(bytes) {
    if (!bytes) return '0 B'
    const units = ['B', 'KB', 'MB', 'GB', 'TB']
    let size = bytes
    let i = 0
    while (size >= 1024 && i < units.length - 1) {
        size /= 1024
        i++
    }
    return size.toFixed(1) + ' ' + units[i]
}
</script>

<template>
    <div class="status-bar">
        <div class="status-left">
            <span v-if="selectedCount > 0" class="status-item">
                已选择 {{ selectedCount }} 项
            </span>
            <span class="status-item">共 {{ fileCount }} 个项目</span>
        </div>
        <div class="status-right" v-if="storage">
            <div class="storage-bar">
                <div class="storage-fill" :style="{
                    width: ((storage.used_space / storage.total_space) * 100) + '%'
                }"></div>
            </div>
            <span class="status-item">
                已用 {{ formatSize(storage.used_space) }} / {{ formatSize(storage.total_space) }}
            </span>
        </div>
    </div>
</template>

<style scoped>
.status-bar {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 6px 20px;
    background: white;
    border-top: 1px solid #e0e0e0;
    font-size: 12px;
    color: #666;
}

.status-left, .status-right {
    display: flex;
    align-items: center;
    gap: 12px;
}

.storage-bar {
    width: 120px;
    height: 6px;
    background: #e0e0e0;
    border-radius: 3px;
    overflow: hidden;
}

.storage-fill {
    height: 100%;
    background: #1976d2;
    transition: width 0.3s;
}
</style>
