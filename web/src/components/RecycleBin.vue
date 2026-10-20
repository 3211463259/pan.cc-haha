<script setup>
import { ref, onMounted } from 'vue'
import * as api from '../api.js'

const emit = defineEmits(['close', 'refresh'])

const items = ref([])
const loading = ref(false)
const selected = ref([])

async function loadItems() {
    loading.value = true
    items.value = await api.listRecycleBin()
    loading.value = false
}

function toggleSelect(item) {
    const idx = selected.value.findIndex(i => i.id === item.id)
    if (idx >= 0) selected.value.splice(idx, 1)
    else selected.value.push(item)
}

async function restoreSelected() {
    for (const item of selected.value) {
        await api.restoreFromRecycle(item.id)
    }
    selected.value = []
    loadItems()
    emit('refresh')
}

async function restoreItem(item) {
    await api.restoreFromRecycle(item.id)
    loadItems()
    emit('refresh')
}

async function purgeItem(item) {
    if (!confirm(`永久删除 "${item.name}"？此操作不可恢复。`)) return
    await api.purgeFromRecycle(item.id)
    loadItems()
}

async function purgeAll() {
    if (items.value.length === 0) return
    if (!confirm('清空回收站？此操作不可恢复。')) return
    for (const item of items.value) {
        await api.purgeFromRecycle(item.id)
    }
    loadItems()
}

function formatSize(bytes) {
    if (bytes === 0) return '-'
    const units = ['B', 'KB', 'MB', 'GB']
    let size = bytes, i = 0
    while (size >= 1024 && i < units.length - 1) { size /= 1024; i++ }
    return size.toFixed(1) + ' ' + units[i]
}

function formatTime(ts) {
    if (!ts) return '-'
    return new Date(ts * 1000).toLocaleString('zh-CN')
}

onMounted(loadItems)
</script>

<template>
    <div class="modal-overlay" @click.self="emit('close')">
        <div class="modal recycle-modal">
            <div class="recycle-header">
                <h3>🗑️ 回收站</h3>
                <button class="icon-btn" @click="emit('close')">✕</button>
            </div>

            <div class="recycle-toolbar">
                <button class="btn btn-primary btn-sm" :disabled="selected.length === 0" @click="restoreSelected">
                    ↩️ 还原选中
                </button>
                <button class="btn btn-danger btn-sm" @click="purgeAll">
                    清空回收站
                </button>
                <span class="recycle-info">文件保留 100 天</span>
            </div>

            <div class="recycle-list" v-if="!loading">
                <div v-for="item in items" :key="item.id" class="recycle-item">
                    <input type="checkbox" :checked="selected.some(i => i.id === item.id)"
                           @change="toggleSelect(item)">
                    <span class="file-icon">{{ item.type === 'dir' ? '📁' : '📎' }}</span>
                    <span class="item-name">{{ item.name }}</span>
                    <span class="item-size">{{ formatSize(item.size) }}</span>
                    <span class="item-time">{{ formatTime(item.deleted_at) }}</span>
                    <button class="icon-btn" title="还原" @click="restoreItem(item)">↩️</button>
                    <button class="icon-btn danger" title="永久删除" @click="purgeItem(item)">🗑️</button>
                </div>
                <div v-if="items.length === 0" class="empty-state">
                    <p>回收站为空</p>
                </div>
            </div>
        </div>
    </div>
</template>

<style scoped>
.recycle-modal {
    width: 650px;
    max-height: 70vh;
    display: flex;
    flex-direction: column;
}

.recycle-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 12px;
}

.recycle-toolbar {
    display: flex;
    align-items: center;
    gap: 8px;
    margin-bottom: 12px;
}

.recycle-info {
    font-size: 12px;
    color: #999;
    margin-left: auto;
}

.recycle-list {
    flex: 1;
    overflow-y: auto;
}

.recycle-item {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 8px 0;
    border-bottom: 1px solid #f0f0f0;
    font-size: 13px;
}

.item-name { flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.item-size { width: 80px; color: #888; }
.item-time { width: 150px; color: #888; font-size: 12px; }

.empty-state {
    text-align: center;
    padding: 40px;
    color: #999;
}
</style>
