<script setup>
import { ref, watch } from 'vue'
import * as api from '../api.js'

const emit = defineEmits(['close', 'download'])

const keyword = ref('')
const results = ref([])
const loading = ref(false)
const typeFilter = ref('')
const minSize = ref('')
const maxSize = ref('')

let searchTimer = null

function doSearch() {
    if (!keyword.value.trim()) {
        results.value = []
        return
    }

    clearTimeout(searchTimer)
    searchTimer = setTimeout(async () => {
        loading.value = true
        try {
            const params = { q: keyword.value }
            if (typeFilter.value) params.type = typeFilter.value
            if (minSize.value) params.min_size = minSize.value
            if (maxSize.value) params.max_size = maxSize.value

            results.value = await api.search(params)
        } catch (e) {
            results.value = []
        }
        loading.value = false
    }, 300)
}

watch([keyword, typeFilter, minSize, maxSize], doSearch)

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
</script>

<template>
    <div class="modal-overlay" @click.self="emit('close')">
        <div class="modal search-modal">
            <div class="search-header">
                <h3>🔍 搜索文件</h3>
                <button class="icon-btn" @click="emit('close')">✕</button>
            </div>

            <!-- 搜索框 -->
            <div class="search-input-row">
                <input
                    v-model="keyword"
                    class="input search-input"
                    placeholder="输入文件名搜索..."
                    autofocus
                />
            </div>

            <!-- 筛选器 -->
            <div class="search-filters">
                <select v-model="typeFilter" class="input" style="width: 100px;">
                    <option value="">全部类型</option>
                    <option value="file">仅文件</option>
                    <option value="dir">仅文件夹</option>
                </select>
                <input v-model="minSize" class="input" style="width: 80px;" placeholder="最小(B)" type="number">
                <span>~</span>
                <input v-model="maxSize" class="input" style="width: 80px;" placeholder="最大(B)" type="number">
            </div>

            <!-- 搜索结果 -->
            <div class="search-results" v-if="!loading">
                <div v-for="item in results" :key="item.id" class="result-item">
                    <span class="file-icon">{{ item.type === 'dir' ? '📁' : '📎' }}</span>
                    <span class="result-name">{{ item.name }}</span>
                    <span class="result-size">{{ formatSize(item.size) }}</span>
                    <span class="result-time">{{ formatTime(item.created_at) }}</span>
                    <button class="icon-btn" @click="emit('download', item)" v-if="item.type === 'file'">⬇️</button>
                </div>
                <div v-if="results.length === 0 && keyword.trim()" class="empty-state">
                    <p>未找到匹配的文件</p>
                </div>
                <div v-if="!keyword.trim()" class="empty-state">
                    <p>输入关键词开始搜索</p>
                </div>
            </div>
            <div v-if="loading" class="empty-state">
                <div class="spinner"></div>
                <p>搜索中...</p>
            </div>
        </div>
    </div>
</template>

<style scoped>
.search-modal {
    width: 600px;
    max-height: 70vh;
    display: flex;
    flex-direction: column;
}

.search-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 12px;
}

.search-input-row {
    margin-bottom: 10px;
}

.search-input {
    width: 100%;
    font-size: 16px;
}

.search-filters {
    display: flex;
    align-items: center;
    gap: 8px;
    margin-bottom: 12px;
    font-size: 13px;
    color: #666;
}

.search-results {
    flex: 1;
    overflow-y: auto;
}

.result-item {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 8px 0;
    border-bottom: 1px solid #f0f0f0;
    font-size: 13px;
}

.result-name { flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
.result-size { width: 80px; color: #888; }
.result-time { width: 150px; color: #888; font-size: 12px; }

.empty-state {
    text-align: center;
    padding: 40px;
    color: #999;
}

.spinner {
    width: 24px;
    height: 24px;
    border: 3px solid #e0e0e0;
    border-top-color: #1976d2;
    border-radius: 50%;
    animation: spin 0.8s linear infinite;
    margin: 0 auto 8px;
}

@keyframes spin {
    to { transform: rotate(360deg); }
}
</style>
