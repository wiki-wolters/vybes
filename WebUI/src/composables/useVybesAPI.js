import { ref } from 'vue'; // Import ref
import VybesAPI from '../api-client.js';

const apiClient = new VybesAPI('http://vybes.local');
const liveUpdateData = ref(null); // Reactive variable for live updates
const isWebSocketConnected = ref(false);

// Function to connect to WebSocket
function connectWebSocket() {
  if (apiClient && typeof apiClient.connectLiveUpdates === 'function') {
    apiClient.connectLiveUpdates(
      (data) => { // onMessage
        console.log('Live update received:', data);
        liveUpdateData.value = data;
        // In a real app, you might want to parse 'data.event' and 'data.payload'
        // and update specific parts of your application state.
      },
      (error) => { // onError
        console.error('WebSocket connection error:', error);
        isWebSocketConnected.value = false;
      },
      (event) => { // onClose
        console.log('WebSocket connection closed:', event);
        isWebSocketConnected.value = false;
        // Optionally, attempt to reconnect here after a delay
      }
    );
    // Assuming onopen within connectLiveUpdates will set a connected status if needed
    // For now, we'll use a simple flag or rely on console logs.
    // The VybesAPI class itself has an onopen that logs, we can add:
    if (apiClient.socket) { // Ensure socket is created before assigning onopen
        apiClient.socket.onopen = () => {
            console.log('WebSocket connection established via useVybesAPI.');
            isWebSocketConnected.value = true;
        };
    }
  } else {
    console.error('API client or connectLiveUpdates function not available.');
  }
}

// Function to disconnect WebSocket
function disconnectWebSocket() {
    if (apiClient && typeof apiClient.disconnectLiveUpdates === 'function') {
        apiClient.disconnectLiveUpdates();
        isWebSocketConnected.value = false; // Ensure status is updated
        console.log('WebSocket connection disconnected via useVybesAPI.');
    }
}

export function useVybesAPI() {
  return {
    apiClient,
    liveUpdateData,       // Expose live data
    isWebSocketConnected, // Expose connection status
    connectWebSocket,     // Expose function to connect
    disconnectWebSocket   // Expose function to disconnect
  };
}
