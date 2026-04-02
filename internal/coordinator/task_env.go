package coordinator

import (
	"fmt"
	"strconv"

	pb "github.com/skewer-project/skewer/api/proto/coordinator/v1"
)

func buildWorkerLaunch(task *Task, coordinatorAddr string) (workerType string, env map[string]string, err error) {
	env = map[string]string{
		"TASK_ID":          task.ID,
		"JOB_ID":           task.JobID,
		"FRAME_ID":         task.FrameID,
		"COORDINATOR_ADDR": coordinatorAddr,
	}
	switch p := task.Payload.(type) {
	case *pb.RenderTask:
		return "skewer", renderTaskEnv(p, env), nil
	case *pb.CompositeTask:
		return "loom", compositeTaskEnv(p, env), nil
	default:
		return "", nil, fmt.Errorf("unknown task payload type")
	}
}

func renderTaskEnv(t *pb.RenderTask, base map[string]string) map[string]string {
	out := base
	out["SCENE_URI"] = t.GetSceneUri()
	out["OUTPUT_URI"] = t.GetOutputUri()
	out["WIDTH"] = strconv.FormatInt(int64(t.GetWidth()), 10)
	out["HEIGHT"] = strconv.FormatInt(int64(t.GetHeight()), 10)
	out["MAX_SAMPLES"] = strconv.FormatInt(int64(t.GetMaxSamples()), 10)
	out["ENABLE_DEEP"] = strconv.FormatBool(t.GetEnableDeep())
	out["THREADS"] = strconv.FormatInt(int64(t.GetThreads()), 10)
	out["NOISE_THRESHOLD"] = strconv.FormatFloat(float64(t.GetNoiseThreshold()), 'f', -1, 32)
	out["MIN_SAMPLES"] = strconv.FormatInt(int64(t.GetMinSamples()), 10)
	out["ADAPTIVE_STEP"] = strconv.FormatInt(int64(t.GetAdaptiveStep()), 10)
	return out
}

func compositeTaskEnv(t *pb.CompositeTask, base map[string]string) map[string]string {
	out := base
	out["OUTPUT_URI"] = t.GetOutputUri()
	out["WIDTH"] = strconv.FormatInt(int64(t.GetWidth()), 10)
	out["HEIGHT"] = strconv.FormatInt(int64(t.GetHeight()), 10)
	layers := t.GetLayerUris()
	out["LAYER_COUNT"] = strconv.Itoa(len(layers))
	for i, u := range layers {
		out[fmt.Sprintf("LAYER_URI_%d", i)] = u
	}
	return out
}
