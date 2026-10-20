<script setup>
import { ref } from 'vue'

defineProps({
    queue: Array
})

const emit = defineEmits(['upload', 'close'])
const dragOver = ref(false)

function handleDrop(e) {
    dragOver.value = false
    const files = e.dataTransfer.files
    if (files.length > 0) {
        emit('upload', Array.from(files))
    }
}
</script>

<template>
    <div class="modal-overlay" @click.self="emit('close')">
        <div class="upload-panel">
            <div class="upload-header">
                <h3>📤 文件上传</h3>
                <button class="icon-btn" @click="emit('close')">✕</button>
            </div>

            <!-- 拖拽区域 -->
            <div
                class="drop-zone"
                :class="{ 'drag-over': dragOver }"
                @drop="handleDrop"
                @dragover.prevent="dragOver = true"
                @dragleave="dragOver = false"
            >
                <div class="drop-icon">📥</div>
                <p>拖拽文件到此处上传</p>
                <p class="hint">支持批量上传，单次最多 1000 个文件</p>
                <button class="btn btn-primary" @click="$emit('upload', [])">
                    选择文件
                </button>
            </div>

            <!-- 上传队列 -->
            <div v-if="queue.length > 0" class="upload-queue">
                <div v-for="task in queue" :key="task.id" class="queue-item">
                    <div class="queue-info">
                        <span class="queue-name">{{ task.name }}</span>
                        <span class="queue-status" :class="task.status">
                            {{ task.status === 'uploading' ? `${task.progress}%` :
                               task.status === 'done' ? '✓ 完成' :
                               task.status === 'error' ? '✗ 失败' : '' }}
                        </span>
                    </div>
                    <div class="progress-bar">
                        <div class="fill" :style="{ width: task.progress + '%' }"></div>
                    </div>
                </div>
            </div>
        </div>
    </div>
</template>

<style scoped>
.upload-panel {
    background: white;
    border-radius: 12px;
    width: 500px;
    max-height: 600px;
    overflow-y: auto;
    box-shadow: 0 8px 32px rgba(0,0,0,0.2);
}

.upload-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 16px 20px;
    border-bottom: 1px solid #eee;
}

.upload-header h3 {
    font-size: 16px;
}

.drop-zone {
    margin: 20px;
    padding: 40px 20px;
    border: 2px dashed #ddd;
    border-radius: 12px;
    text-align: center;
    transition: all 0.2s;
}

.drop-zone.drag-over {
    border-color: #1976d2;
    background: #e3f2fd;
}

.drop-icon {
    font-size: 48px;
    margin-bottom: 12px;
}

.hint {
    font-size: 12px;
    color: #999;
    margin: 8px 0 16px;
}

.upload-queue {
    padding: 0 20px 20px;
}

.queue-item {
    padding: 10px 0;
    border-bottom: 1px solid #f0f0f0;
}

.queue-info {
    display: flex;
    justify-content: space-between;
    margin-bottom: 6px;
}

.queue-name {
    font-size: 13px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    max-width: 350px;
}

.queue-status {
    font-size: 12px;
    font-weight: 600;
}

.queue-status.done { color: #2e7d32; }
.queue-status.error { color: #d32f2f; }
.queue-status.uploading { color: #1976d2; }
</style>
