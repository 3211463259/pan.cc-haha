import axios from 'axios'

const api = axios.create({
    baseURL: '/api',
    timeout: 30000
})

// 文件列表
export async function listFiles(parentId = 0, sortBy = 'name', sortOrder = 'asc') {
    const res = await api.get('/files', {
        params: { parent_id: parentId, sort_by: sortBy, sort_order: sortOrder }
    })
    return res.data.success ? res.data.data : []
}

// 创建文件夹
export async function createFolder(parentId, name) {
    const res = await api.post('/folder', { parent_id: parentId, name })
    return res.data
}

// 上传文件
export async function uploadFile(parentId, file, onProgress) {
    const formData = new FormData()
    formData.append('file', file)
    formData.append('filename', file.name)

    const res = await api.post('/upload?parent_id=' + parentId, formData, {
        headers: { 'Content-Type': 'multipart/form-data' },
        onUploadProgress: (e) => {
            if (onProgress && e.total) {
                onProgress(Math.round((e.loaded / e.total) * 100))
            }
        }
    })
    return res.data
}

// 分块上传
export async function uploadChunked(parentId, file, onProgress) {
    const chunkSize = 5 * 1024 * 1024 // 5MB
    const totalChunks = Math.ceil(file.size / chunkSize)

    // 创建上传会话
    const initRes = await api.post('/upload/session', {
        parent_id: parentId,
        file_name: file.name,
        file_size: file.size,
        chunk_size: chunkSize
    })

    if (!initRes.data.success) throw new Error(initRes.data.message)

    const uploadId = initRes.data.data.upload_id

    for (let i = 0; i < totalChunks; i++) {
        const start = i * chunkSize
        const end = Math.min(start + chunkSize, file.size)
        const chunk = file.slice(start, end)

        const formData = new FormData()
        formData.append('chunk', chunk)

        await api.post('/upload/chunk', formData, {
            headers: {
                'X-Upload-Id': uploadId,
                'X-Chunk-Index': i,
                'Content-Type': 'multipart/form-data'
            }
        })

        if (onProgress) {
            onProgress(Math.round(((i + 1) / totalChunks) * 100))
        }
    }

    // 完成上传
    const finishRes = await api.post('/upload/complete', {
        upload_id: uploadId,
        parent_id: parentId,
        file_name: file.name,
        file_size: file.size
    })

    return finishRes.data
}

// 下载文件
export function getDownloadUrl(fileId) {
    return `/api/download/${fileId}`
}

// 删除文件
export async function deleteFile(fileId) {
    const res = await api.delete(`/files/${fileId}`)
    return res.data
}

// 重命名
export async function rename(id, name) {
    const res = await api.put('/rename', { id, name })
    return res.data
}

// 移动
export async function moveFile(id, targetParentId) {
    const res = await api.put('/move', { id, target_parent_id: targetParentId })
    return res.data
}

// 复制
export async function copyFile(id, targetParentId) {
    const res = await api.post('/copy', { id, target_parent_id: targetParentId })
    return res.data
}

// 搜索
export async function search(params) {
    const res = await api.get('/search', { params })
    return res.data.success ? res.data.data : []
}

// 回收站列表
export async function listRecycleBin() {
    const res = await api.get('/recycle')
    return res.data.success ? res.data.data : []
}

// 还原
export async function restoreFromRecycle(id, targetParentId = 0) {
    const res = await api.post('/recycle/restore', { id, target_parent_id: targetParentId })
    return res.data
}

// 永久删除
export async function purgeFromRecycle(id) {
    const res = await api.delete(`/recycle/${id}`)
    return res.data
}

// 文件详情
export async function getFileInfo(fileId) {
    const res = await api.get(`/info/${fileId}`)
    return res.data.success ? res.data.data : null
}

// 文件夹详情
export async function getFolderInfo(folderId = 0) {
    const res = await api.get('/folder_info', { params: { id: folderId } })
    return res.data.success ? res.data.data : null
}

// 存储信息
export async function getStorageInfo() {
    const res = await api.get('/storage')
    return res.data.success ? res.data.data : null
}
